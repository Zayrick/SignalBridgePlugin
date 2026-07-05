#include "openrgb/SignalBridgeController.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>

#include "domain/ControlParameters.h"
#include "domain/PathUtils.h"
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

device_type ResolveOpenRgbDeviceType(const std::string& signalrgb_type)
{
    const std::string normalized = LowerAscii(signalrgb_type);
    if(normalized == "keyboard")
    {
        return DEVICE_TYPE_KEYBOARD;
    }
    if(normalized == "mouse")
    {
        return DEVICE_TYPE_MOUSE;
    }
    if(normalized == "mousepad")
    {
        return DEVICE_TYPE_MOUSEMAT;
    }
    if(normalized == "lightingcontroller")
    {
        return DEVICE_TYPE_LEDSTRIP;
    }
    if(normalized == "headphones" || normalized == "headset")
    {
        return DEVICE_TYPE_HEADSET;
    }
    if(normalized == "microphone")
    {
        return DEVICE_TYPE_MICROPHONE;
    }
    if(normalized == "speakers")
    {
        return DEVICE_TYPE_SPEAKER;
    }
    if(normalized == "monitor")
    {
        return DEVICE_TYPE_MONITOR;
    }
    if(normalized == "gpu")
    {
        return DEVICE_TYPE_GPU;
    }
    if(normalized == "motherboard")
    {
        return DEVICE_TYPE_MOTHERBOARD;
    }
    if(normalized == "ram")
    {
        return DEVICE_TYPE_DRAM;
    }
    if(normalized == "cooler" || normalized == "aio" || normalized == "fan")
    {
        return DEVICE_TYPE_COOLER;
    }
    if(normalized == "ledstrip")
    {
        return DEVICE_TYPE_LEDSTRIP;
    }
    if(normalized == "speaker")
    {
        return DEVICE_TYPE_SPEAKER;
    }
    if(normalized == "accessory" || normalized == "chair" || normalized == "case" || normalized == "dongle" || normalized == "lcd")
    {
        return DEVICE_TYPE_ACCESSORY;
    }
    return DEVICE_TYPE_UNKNOWN;
}

RuntimeChannelState* FindRuntimeChannel(RuntimeDeviceState& device, const std::string& name)
{
    const auto it = std::find_if(device.channels.begin(), device.channels.end(), [&](const RuntimeChannelState& channel) {
        return channel.name == name;
    });
    return it != device.channels.end() ? &*it : nullptr;
}

RuntimeSubdeviceState* FindRuntimeSubdevice(RuntimeDeviceState& device, const std::string& name)
{
    const auto it = std::find_if(device.subdevices.begin(), device.subdevices.end(), [&](const RuntimeSubdeviceState& subdevice) {
        return subdevice.name == name;
    });
    return it != device.subdevices.end() ? &*it : nullptr;
}

std::size_t RuntimeSubdeviceColorBytes(const RuntimeSubdeviceState& subdevice)
{
    return std::max(subdevice.led_names.size(), subdevice.led_positions.size()) * 3;
}

void ClearMainFrame(RuntimeDeviceState& device)
{
    device.main_frame.colors.clear();
    device.main_frame.led_count = 0;
    device.main_frame.width = std::max(1, device.width);
}

void ClearChannelFrame(RuntimeChannelState& channel)
{
    channel.colors.clear();
    channel.led_count = 0;
    channel.needs_pulse = true;
}

void ClearSubdeviceFrame(RuntimeSubdeviceState& subdevice)
{
    subdevice.colors.assign(RuntimeSubdeviceColorBytes(subdevice), 0);
}

void FinalizeChannelFrame(RuntimeChannelState& channel)
{
    std::size_t led_count = channel.colors.size() / 3;
    if(channel.led_limit > 0 && led_count > static_cast<std::size_t>(channel.led_limit))
    {
        led_count = static_cast<std::size_t>(channel.led_limit);
        channel.colors.resize(led_count * 3);
    }
    channel.led_count = static_cast<int>(led_count);
    channel.needs_pulse = channel.led_count == 0;
}

void FinalizeSubdeviceFrame(RuntimeSubdeviceState& subdevice)
{
    subdevice.colors.resize(RuntimeSubdeviceColorBytes(subdevice), 0);
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

SignalBridgeController::SignalBridgeController(
    std::shared_ptr<SerialBackend> serial_backend,
    ScriptMeta meta,
    SerialInfo primary_serial,
    QJsonObject configuration,
    std::string config_key,
    ScriptLogCallback log_callback)
    : serial_backend_(std::move(serial_backend))
    , meta_(std::move(meta))
    , primary_serial_(std::move(primary_serial))
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
    description = "SignalRGB serial script device";
    version = "SignalRGB Bridge";
    serial = primary_serial_.serial_number;
    location = !primary_serial_.port_name.empty() ? "Serial: " + primary_serial_.port_name : primary_serial_.system_location;
    type = ResolveOpenRgbDeviceType(meta_.device_type);
    flags = CONTROLLER_FLAG_LOCAL;

    mode direct;
    direct.name = "Direct";
    direct.value = 0;
    direct.flags = MODE_FLAG_HAS_PER_LED_COLOR;
    direct.color_mode = MODE_COLORS_PER_LED;
    modes.push_back(direct);
    active_mode = 0;

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

        RuntimeCallbackState* runtime_state = runtime_->MutableState();
        if(runtime_state == nullptr)
        {
            return;
        }
        RuntimeDeviceState& device = runtime_state->device;
        ClearMainFrame(device);
        for(RuntimeChannelState& channel : device.channels)
        {
            ClearChannelFrame(channel);
        }
        for(RuntimeSubdeviceState& subdevice : device.subdevices)
        {
            ClearSubdeviceFrame(subdevice);
        }

        for(std::size_t zone_idx = 0; zone_idx < zones.size() && zone_idx < zone_targets_.size(); zone_idx++)
        {
            const ZoneTarget& target = zone_targets_[zone_idx];
            switch(target.kind)
            {
            case ZoneTarget::Kind::Main:
                BuildFrameForZone(zones, colors, static_cast<unsigned int>(zone_idx), target, device.main_frame);
                break;
            case ZoneTarget::Kind::Channel:
                if(RuntimeChannelState* channel = FindRuntimeChannel(device, target.key))
                {
                    BuildFrameColorsForZone(zones, colors, static_cast<unsigned int>(zone_idx), target, channel->colors);
                    FinalizeChannelFrame(*channel);
                }
                break;
            case ZoneTarget::Kind::Subdevice:
                if(RuntimeSubdeviceState* subdevice = FindRuntimeSubdevice(device, target.key))
                {
                    BuildFrameColorsForZone(zones, colors, static_cast<unsigned int>(zone_idx), target, subdevice->colors);
                    FinalizeSubdeviceFrame(*subdevice);
                }
                break;
            }
        }

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

const std::string& SignalBridgeController::ConfigKey() const
{
    return config_key_;
}

const ScriptMeta& SignalBridgeController::ScriptMetadata() const
{
    return meta_;
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
    const std::map<std::string, HidBackend::Handle> endpoint_handles =
        endpoint_session_ != nullptr ? endpoint_session_->Handles() : std::map<std::string, HidBackend::Handle>();
    const std::vector<EndpointDescriptor> endpoints =
        endpoint_session_ != nullptr ? endpoint_session_->Endpoints() : std::vector<EndpointDescriptor>();

    runtime_ = std::make_unique<QuickJsRuntime>(SignalRgbRuntimeFactory::CreateDeviceRuntime(
        hid_backend_,
        meta_,
        endpoint_session_ != nullptr ? endpoint_session_->PrimaryHandle() : 0,
        primary_hid_,
        endpoint_handles,
        endpoints,
        configuration_,
        log_callback_,
        serial_backend_,
        primary_serial_));
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
