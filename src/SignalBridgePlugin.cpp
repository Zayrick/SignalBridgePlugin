#include "SignalBridge/SignalBridgePlugin.h"

#include <atomic>
#include <memory>
#include <thread>

#include "config/DeviceConfigStore.h"
#include "discovery/ControllerRegistry.h"
#include "discovery/DiscoveryService.h"
#include "ui/SignalBridgeWidget.h"

namespace signalbridge
{
class SignalBridgePluginCore
{
public:
    explicit SignalBridgePluginCore(SignalBridgePlugin* plugin)
        : plugin_(plugin)
    {
    }

    ~SignalBridgePluginCore()
    {
        StopDiscoveryThread();
        registry_.Clear(resource_manager_);
    }

    void Load(ResourceManagerInterface* manager)
    {
        resource_manager_ = manager;
        config_store_.Load(resource_manager_);
        EnsureWidget();
        widget_->SetResourceAvailable(true);
        widget_->ClearLogOutput();
        DiscoverSignalRgbDevices();
    }

    QWidget* GetWidget()
    {
        EnsureWidget();
        return widget_.get();
    }

    void Unload()
    {
        StopDiscoveryThread();
        registry_.Clear(resource_manager_);
        resource_manager_ = nullptr;
        config_store_.Reset();

        if(widget_ != nullptr)
        {
            widget_->SetResourceAvailable(false);
            widget_->ClearLogOutput();
            widget_->ApplyDiscoveryStatus(
                QStringLiteral("Plugin unloaded"),
                QString(),
                QStringList(),
                QStringLiteral("[]"),
                false,
                0);
        }
    }

    void DiscoverSignalRgbDevices()
    {
        if(resource_manager_ == nullptr)
        {
            return;
        }

        EnsureWidget();
        if(discovery_running_.load())
        {
            widget_->ApplyDiscoveryStatus(
                QStringLiteral("SignalRGB discovery is already running"),
                QString(),
                QStringList(),
                QString(),
                true,
                discovery_progress_);
            return;
        }

        if(discovery_thread_.joinable())
        {
            discovery_thread_.join();
        }

        discovery_cancel_requested_.store(false);
        const int generation = discovery_generation_.fetch_add(1) + 1;
        discovery_running_.store(true);
        widget_->ClearLogOutput();
        plugin_->EmitDiscoveryStatus(
            generation,
            QStringLiteral("Scanning SignalRGB scripts..."),
            QString(),
            QStringList(),
            QStringLiteral("[]"),
            true,
            0);

        discovery_thread_ = std::thread(&SignalBridgePluginCore::DiscoveryWorker, this, generation, resource_manager_);
    }

    void ApplyDiscoveryStatus(
        int generation,
        const QString& status,
        const QString& details,
        const QStringList& scripts,
        const QString& devices,
        bool running,
        int progress)
    {
        if(generation != discovery_generation_.load())
        {
            return;
        }

        discovery_progress_ = progress;
        if(widget_ != nullptr)
        {
            widget_->ApplyDiscoveryStatus(status, details, scripts, devices, running, progress);
        }
    }

    void AppendLogLine(const QString& line)
    {
        if(widget_ != nullptr)
        {
            widget_->AppendLogLine(line);
        }
    }

private:
    void EnsureWidget()
    {
        if(widget_ != nullptr)
        {
            return;
        }

        widget_ = std::make_unique<SignalBridgeWidget>(
            [this](const QString& key, const QString& script_key) {
                return config_store_.ConfigurationForDevice(key, script_key);
            },
            [this](const QString& key, const QString& property, const QJsonValue& value) {
                SetDeviceConfigurationValue(key, property, value);
            },
            [this]() {
                DiscoverSignalRgbDevices();
            });
        widget_->SetResourceAvailable(resource_manager_ != nullptr && !discovery_running_.load());
    }

    void DiscoveryWorker(int generation, ResourceManagerInterface* manager)
    {
        DiscoveryCallbacks callbacks;
        callbacks.is_stale = [this](int callback_generation) {
            return IsDiscoveryStale(callback_generation);
        };
        callbacks.status_changed =
            [this](
                int callback_generation,
                const QString& status,
                const QString& details,
                const QStringList& scripts,
                const QString& devices,
                bool running,
                int progress) {
                plugin_->EmitDiscoveryStatus(callback_generation, status, details, scripts, devices, running, progress);
            };
        callbacks.log_line = [this](const QString& line) {
            plugin_->EmitScriptLog(line);
        };

        discovery_service_.Discover(generation, manager, registry_, config_store_, callbacks);
        discovery_running_.store(false);
    }

    void StopDiscoveryThread()
    {
        discovery_cancel_requested_.store(true);
        discovery_generation_.fetch_add(1);
        if(discovery_thread_.joinable())
        {
            discovery_thread_.join();
        }
        discovery_running_.store(false);
    }

    bool IsDiscoveryStale(int generation) const
    {
        return discovery_cancel_requested_.load() || generation != discovery_generation_.load();
    }

    void SetDeviceConfigurationValue(const QString& key, const QString& property, const QJsonValue& value)
    {
        config_store_.SetDeviceConfigurationValue(key, property, value);
        registry_.ApplyConfiguration(key, property, value);
    }

    SignalBridgePlugin* plugin_ = nullptr;
    ResourceManagerInterface* resource_manager_ = nullptr;
    DeviceConfigStore config_store_;
    ControllerRegistry registry_;
    DiscoveryService discovery_service_;
    std::unique_ptr<SignalBridgeWidget> widget_;
    std::thread discovery_thread_;
    std::atomic<bool> discovery_running_{ false };
    std::atomic<bool> discovery_cancel_requested_{ false };
    std::atomic<int> discovery_generation_{ 0 };
    int discovery_progress_ = 0;
};
}

SignalBridgePlugin::SignalBridgePlugin(QObject* parent)
    : QObject(parent)
    , core_(std::make_unique<signalbridge::SignalBridgePluginCore>(this))
{
    connect(this,
            &SignalBridgePlugin::DiscoveryStatusChanged,
            this,
            &SignalBridgePlugin::ApplyDiscoveryStatus,
            Qt::QueuedConnection);
    connect(this,
            &SignalBridgePlugin::ScriptLogReceived,
            this,
            &SignalBridgePlugin::AppendLogLine,
            Qt::QueuedConnection);
}

SignalBridgePlugin::~SignalBridgePlugin() = default;

OpenRGBPluginInfo SignalBridgePlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;

    info.Name = "Signal Bridge Plugin";
    info.Description = "Brings support for SignalRGB JavaScript device plugins to OpenRGB.";
    info.Version = SIGNALBRIDGEPLUGIN_VERSION;
    info.Commit = SIGNALBRIDGEPLUGIN_COMMIT;
    info.URL = "";
    info.Location = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label = "SignalBridge";

    return info;
}

unsigned int SignalBridgePlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void SignalBridgePlugin::Load(ResourceManagerInterface* resource_manager_ptr)
{
    core_->Load(resource_manager_ptr);
}

QWidget* SignalBridgePlugin::GetWidget()
{
    return core_->GetWidget();
}

QMenu* SignalBridgePlugin::GetTrayMenu()
{
    return nullptr;
}

void SignalBridgePlugin::Unload()
{
    core_->Unload();
}

void SignalBridgePlugin::ApplyDiscoveryStatus(
    int generation,
    const QString& status,
    const QString& details,
    const QStringList& scripts,
    const QString& devices,
    bool running,
    int progress)
{
    core_->ApplyDiscoveryStatus(generation, status, details, scripts, devices, running, progress);
}

void SignalBridgePlugin::AppendLogLine(const QString& line)
{
    core_->AppendLogLine(line);
}

void SignalBridgePlugin::EmitDiscoveryStatus(
    int generation,
    const QString& status,
    const QString& details,
    const QStringList& scripts,
    const QString& devices,
    bool running,
    int progress)
{
    emit DiscoveryStatusChanged(generation, status, details, scripts, devices, running, progress);
}

void SignalBridgePlugin::EmitScriptLog(const QString& line)
{
    emit ScriptLogReceived(line);
}
