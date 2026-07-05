#include "SignalBridge/SignalBridgePlugin.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <QMetaObject>

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
        unloading_.store(true);
        StopDiscoveryThread();
        WaitForOpenRgbDetection();
        registry_.UnregisterAndDeleteControllers(resource_manager_.load());
        WaitForOpenRgbDetection();
        UnregisterOpenRgbCallbacks();
    }

    void Load(ResourceManagerInterface* manager)
    {
        resource_manager_.store(manager);
        unloading_.store(false);
        openrgb_detection_running_.store(false);
        pending_discovery_after_openrgb_detection_.store(false);
        RegisterOpenRgbCallbacks();
        config_store_.Load(resource_manager_.load());
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
        unloading_.store(true);
        StopDiscoveryThread();
        WaitForOpenRgbDetection();
        registry_.UnregisterAndDeleteControllers(resource_manager_.load());
        WaitForOpenRgbDetection();
        UnregisterOpenRgbCallbacks();
        resource_manager_.store(nullptr);
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
        ResourceManagerInterface* manager = resource_manager_.load();
        if(manager == nullptr)
        {
            return;
        }

        EnsureWidget();
        if(openrgb_detection_running_.load())
        {
            pending_discovery_after_openrgb_detection_.store(true);
            widget_->ApplyDiscoveryStatus(
                QStringLiteral("Waiting for OpenRGB device scan to finish"),
                QString(),
                QStringList(),
                QStringLiteral("[]"),
                false,
                0);
            return;
        }

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
        emit plugin_->DiscoveryStatusChanged(
            generation,
            QStringLiteral("Scanning SignalRGB scripts..."),
            QString(),
            QStringList(),
            QStringLiteral("[]"),
            true,
            0);

        discovery_thread_ = std::thread(&SignalBridgePluginCore::DiscoveryWorker, this, generation, manager);
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
        widget_->SetResourceAvailable(resource_manager_.load() != nullptr && !discovery_running_.load());
    }

    void DiscoveryWorker(int generation, ResourceManagerInterface* manager)
    {
        if(manager != nullptr)
        {
            manager->WaitForDeviceDetection();
        }
        if(IsDiscoveryStale(generation) || openrgb_detection_running_.load())
        {
            discovery_running_.store(false);
            return;
        }

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
                emit plugin_->DiscoveryStatusChanged(callback_generation, status, details, scripts, devices, running, progress);
            };
        callbacks.log_line = [this](const QString& line) {
            emit plugin_->ScriptLogReceived(line);
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

    void WaitForOpenRgbDetection()
    {
        while(openrgb_detection_running_.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool IsDiscoveryStale(int generation) const
    {
        return discovery_cancel_requested_.load() || generation != discovery_generation_.load();
    }

    void SetDeviceConfigurationValue(const QString& key, const QString& property, const QJsonValue& value)
    {
        if(config_store_.SetDeviceConfigurationValue(key, property, value))
        {
            registry_.ApplyConfiguration(resource_manager_.load(), key, property, value);
        }
    }

    static void OpenRgbDetectionStartCallback(void* context)
    {
        if(context != nullptr)
        {
            static_cast<SignalBridgePluginCore*>(context)->OnOpenRgbDetectionStarted();
        }
    }

    static void OpenRgbDetectionEndCallback(void* context)
    {
        if(context != nullptr)
        {
            static_cast<SignalBridgePluginCore*>(context)->OnOpenRgbDetectionEnded();
        }
    }

    void RegisterOpenRgbCallbacks()
    {
        ResourceManagerInterface* manager = resource_manager_.load();
        if(manager == nullptr || callbacks_registered_)
        {
            return;
        }

        manager->RegisterDetectionStartCallback(OpenRgbDetectionStartCallback, this);
        manager->RegisterDetectionEndCallback(OpenRgbDetectionEndCallback, this);
        callbacks_registered_ = true;
    }

    void UnregisterOpenRgbCallbacks()
    {
        ResourceManagerInterface* manager = resource_manager_.load();
        if(manager == nullptr || !callbacks_registered_)
        {
            return;
        }

        manager->UnregisterDetectionStartCallback(OpenRgbDetectionStartCallback, this);
        manager->UnregisterDetectionEndCallback(OpenRgbDetectionEndCallback, this);
        callbacks_registered_ = false;
    }

    void OnOpenRgbDetectionStarted()
    {
        openrgb_detection_running_.store(true);
        pending_discovery_after_openrgb_detection_.store(!unloading_.load());
        StopDiscoveryThread();
        registry_.AbandonOpenRgbOwnedControllers();

        emit plugin_->DiscoveryStatusChanged(
            discovery_generation_.load(),
            QStringLiteral("OpenRGB device scan in progress"),
            QString(),
            QStringList(),
            QStringLiteral("[]"),
            false,
            0);
    }

    void OnOpenRgbDetectionEnded()
    {
        const bool should_discover =
            pending_discovery_after_openrgb_detection_.exchange(false) &&
            !unloading_.load() &&
            resource_manager_.load() != nullptr;

        if(should_discover)
        {
            QMetaObject::invokeMethod(
                plugin_,
                [this]() {
                    if(!unloading_.load())
                    {
                        DiscoverSignalRgbDevices();
                    }
                },
                Qt::QueuedConnection);
        }

        openrgb_detection_running_.store(false);
    }

    SignalBridgePlugin* plugin_ = nullptr;
    std::atomic<ResourceManagerInterface*> resource_manager_{ nullptr };
    DeviceConfigStore config_store_;
    ControllerRegistry registry_;
    DiscoveryService discovery_service_;
    std::unique_ptr<SignalBridgeWidget> widget_;
    std::thread discovery_thread_;
    std::atomic<bool> discovery_running_{ false };
    std::atomic<bool> discovery_cancel_requested_{ false };
    std::atomic<bool> openrgb_detection_running_{ false };
    std::atomic<bool> pending_discovery_after_openrgb_detection_{ false };
    std::atomic<bool> unloading_{ false };
    std::atomic<int> discovery_generation_{ 0 };
    bool callbacks_registered_ = false;
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
    info.URL = SIGNALBRIDGEPLUGIN_URL;
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

