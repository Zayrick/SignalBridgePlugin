#include "openrgb/SignalBridgeController.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>

#include "domain/ControlParameters.h"
#include "openrgb/DeviceTypeMapper.h"
#include "openrgb/FrameBuilder.h"
#include "runtime/SignalRgbRuntimeFactory.h"

namespace signalbridge
{
namespace
{
std::string FirstWord(const std::string& value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if(first, value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    return first == value.end() ? std::string("SignalRGB") : std::string(first, last);
}
}

SignalBridgeController::SignalBridgeController(
    std::shared_ptr<HidBackend> hid_backend,
    ScriptMeta meta,
    HidInfo primary_hid,
    QJsonObject configuration,
    std::string config_key,
    ScriptLogCallback log_callback)
    : hid_backend_(std::move(hid_backend))
    , meta_(std::move(meta))
    , primary_hid_(std::move(primary_hid))
    , configuration_(std::move(configuration))
    , config_key_(std::move(config_key))
    , log_callback_(std::move(log_callback))
{
    if(config_key_.empty())
    {
        config_key_ = meta_.lookup_path.empty() ? meta_.source_path : meta_.lookup_path;
    }

    name = meta_.name;
    vendor = meta_.publisher.empty() ? FirstWord(meta_.name) : meta_.publisher;
    description = "SignalRGB script device";
    version = "SignalRGB Bridge";
    serial = primary_hid_.serial;
    location = primary_hid_.path;
    type = ResolveOpenRgbDeviceType(meta_.device_type);
    flags = CONTROLLER_FLAG_LOCAL;

    mode direct;
    direct.name = "Direct";
    direct.value = 0;
    direct.flags = MODE_FLAG_HAS_PER_LED_COLOR;
    direct.color_mode = MODE_COLORS_PER_LED;
    modes.push_back(direct);
    active_mode = 0;

    endpoint_session_ = std::make_unique<EndpointSession>(hid_backend_, primary_hid_);
    CreateRuntime();
    InitializeScript();
    SetupColors();
}

SignalBridgeController::~SignalBridgeController()
{
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
    if(runtime_ != nullptr && runtime_->HasModuleExport("Shutdown"))
    {
        try
        {
            QJsonArray args;
            args.append(false);
            runtime_->CallModuleExportJson("Shutdown", args);
        }
        catch(...)
        {
        }
    }
    runtime_.reset();
    endpoint_session_.reset();
    DeleteZoneMaps(zones);
}

void SignalBridgeController::SetupZones()
{
}

void SignalBridgeController::ResizeZone(int zone_index, int new_size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(zone_index < 0 || static_cast<std::size_t>(zone_index) >= zones.size())
    {
        return;
    }
    zone& target_zone = zones[static_cast<std::size_t>(zone_index)];
    const unsigned int clamped = static_cast<unsigned int>(std::clamp(
        new_size,
        static_cast<int>(target_zone.leds_min),
        static_cast<int>(target_zone.leds_max)));
    if(target_zone.leds_count == clamped)
    {
        return;
    }
    target_zone.leds_count = clamped;
    RebuildOpenRgbLedList(zones, zone_targets_, type, leds, led_alt_names);
    SetupColors();
}

void SignalBridgeController::DeviceUpdateLEDs()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(shutting_down_ || runtime_ == nullptr)
    {
        return;
    }

    try
    {
        DrainPendingConfigurationChanges();

        QJsonObject main_frame;
        QJsonObject channel_frames;
        QJsonObject subdevice_frames;

        for(std::size_t zone_idx = 0; zone_idx < zones.size() && zone_idx < zone_targets_.size(); zone_idx++)
        {
            const ZoneTarget& target = zone_targets_[zone_idx];
            const QJsonObject frame = BuildFrameForZone(zones, colors, static_cast<unsigned int>(zone_idx), target);
            switch(target.kind)
            {
            case ZoneTarget::Kind::Main:
                main_frame = frame;
                break;
            case ZoneTarget::Kind::Channel:
                channel_frames.insert(QString::fromStdString(target.key), frame);
                break;
            case ZoneTarget::Kind::Subdevice:
                subdevice_frames.insert(QString::fromStdString(target.key), frame);
                break;
            }
        }

        runtime_->ApplyFrames(main_frame, channel_frames, subdevice_frames);

        if(runtime_->HasModuleExport("Render"))
        {
            runtime_->CallModuleExportJson("Render");
        }
    }
    catch(...)
    {
    }
}

void SignalBridgeController::UpdateZoneLEDs(int)
{
    DeviceUpdateLEDs();
}

void SignalBridgeController::UpdateSingleLED(int)
{
    DeviceUpdateLEDs();
}

void SignalBridgeController::SetCustomMode()
{
    active_mode = 0;
}

void SignalBridgeController::DeviceUpdateMode()
{
}

const std::string& SignalBridgeController::SourcePath() const
{
    return meta_.source_path;
}

const std::string& SignalBridgeController::ConfigKey() const
{
    return config_key_;
}

const ScriptMeta& SignalBridgeController::ScriptMetadata() const
{
    return meta_;
}

void SignalBridgeController::SetConfiguration(QJsonObject configuration)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_configuration_changes_.clear();
    for(const QJsonObject& parameter : meta_.control_parameters)
    {
        const QString property = parameter.value("property").toString();
        if(property.isEmpty())
        {
            continue;
        }

        if(NormalizeParameterValue(parameter, configuration_.value(property)) !=
           NormalizeParameterValue(parameter, configuration.value(property)))
        {
            pending_configuration_changes_.push_back(property);
        }
    }
    configuration_ = std::move(configuration);
}

void SignalBridgeController::SetConfigurationValue(const QString& property, const QJsonValue& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(property.isEmpty() || (configuration_.contains(property) && configuration_.value(property) == value))
    {
        return;
    }

    configuration_.insert(property, value);
    pending_configuration_changes_.erase(
        std::remove(pending_configuration_changes_.begin(), pending_configuration_changes_.end(), property),
        pending_configuration_changes_.end());
    pending_configuration_changes_.push_back(property);
}

void SignalBridgeController::DrainPendingConfigurationChanges()
{
    if(runtime_ == nullptr || pending_configuration_changes_.empty())
    {
        return;
    }

    std::vector<QString> changes;
    changes.swap(pending_configuration_changes_);

    for(const QString& property : changes)
    {
        try
        {
            runtime_->ApplyConfigurationChange(meta_, configuration_, property);
        }
        catch(const std::exception& err)
        {
            LogConfigurationError(property, err.what());
        }
        catch(...)
        {
            LogConfigurationError(property, "unknown error");
        }
    }

    try
    {
        const QJsonObject topology = runtime_->TakeTopologyUpdate(false);
        if(!topology.isEmpty())
        {
            BuildZonesFromTopology(topology);
            SetupColors();
        }
    }
    catch(const std::exception& err)
    {
        LogConfigurationError(QString(), std::string("failed to rebuild topology after configuration change: ") + err.what());
    }
    catch(...)
    {
        LogConfigurationError(QString(), "failed to rebuild topology after configuration change: unknown error");
    }
}

void SignalBridgeController::LogConfigurationError(const QString& property, const std::string& details) const
{
    if(!log_callback_)
    {
        return;
    }

    std::string message = "Failed to apply configuration";
    if(!property.isEmpty())
    {
        const QByteArray property_bytes = property.toUtf8();
        message += " \"";
        message += property_bytes.constData();
        message += "\"";
    }
    message += ": ";
    message += details;
    log_callback_(meta_.lookup_path.empty() ? meta_.source_path : meta_.lookup_path, message);
}

void SignalBridgeController::CreateRuntime()
{
    runtime_ = std::make_unique<QuickJsRuntime>(SignalRgbRuntimeFactory::CreateDeviceRuntime(
        hid_backend_,
        meta_,
        endpoint_session_->PrimaryHandle(),
        primary_hid_,
        endpoint_session_->Handles(),
        endpoint_session_->Endpoints(),
        configuration_,
        log_callback_));
}

void SignalBridgeController::InitializeScript()
{
    if(runtime_->HasModuleExport("Initialize"))
    {
        runtime_->CallModuleExportJson("Initialize");
    }

    try
    {
        MergeControlParameters(meta_.control_parameters, runtime_->ExportProperties());
        runtime_->ApplyConfiguration(meta_, configuration_);
    }
    catch(...)
    {
    }

    BuildZonesFromTopology(runtime_->TakeTopologyUpdate(true));
}

void SignalBridgeController::BuildZonesFromTopology(const QJsonObject& topology)
{
    DeleteZoneMaps(zones);
    TopologyResult mapped = BuildOpenRgbTopology(topology, meta_, name);
    name = mapped.controller_name;
    zones = std::move(mapped.zones);
    zone_targets_ = std::move(mapped.targets);
    RebuildOpenRgbLedList(zones, zone_targets_, type, leds, led_alt_names);
}
}
