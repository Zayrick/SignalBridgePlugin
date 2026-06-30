#include "RGBController_SignalBridgeScript.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace
{
constexpr unsigned int kMatrixDisabled = 0xFFFFFFFF;

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

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

unsigned int JsonUInt(const QJsonObject& object, const char* key, unsigned int fallback)
{
    const QJsonValue value = object.value(key);
    if(!value.isDouble())
    {
        return fallback;
    }
    const double raw = value.toDouble();
    return raw >= 0.0 ? static_cast<unsigned int>(raw) : fallback;
}

std::vector<std::pair<int, int>> ExtractPositions(const QJsonValue& value)
{
    std::vector<std::pair<int, int>> positions;
    for(const QJsonValue& item : value.toArray())
    {
        if(item.isArray())
        {
            const QJsonArray array = item.toArray();
            if(array.size() >= 2)
            {
                positions.push_back({ array.at(0).toInt(), array.at(1).toInt() });
            }
        }
        else if(item.isObject())
        {
            const QJsonObject object = item.toObject();
            positions.push_back({ object.value("x").toInt(), object.value("y").toInt() });
        }
    }
    return positions;
}

std::vector<std::string> ExtractNames(const QJsonValue& value)
{
    std::vector<std::string> names;
    for(const QJsonValue& item : value.toArray())
    {
        names.push_back(item.toString().toStdString());
    }
    return names;
}

std::string Trim(const std::string& value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    if(first == value.end())
    {
        return {};
    }

    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return std::string(first, last);
}

bool StartsWithKeyPrefix(const std::string& value)
{
    constexpr const char* prefix = "key:";
    constexpr std::size_t prefix_size = 4;
    if(value.size() < prefix_size)
    {
        return false;
    }

    for(std::size_t idx = 0; idx < prefix_size; idx++)
    {
        if(std::tolower(static_cast<unsigned char>(value[idx])) != prefix[idx])
        {
            return false;
        }
    }
    return true;
}

std::string OpenRgbLedName(device_type type, const std::string& raw_name)
{
    const std::string name = Trim(raw_name);
    if(name.empty())
    {
        return {};
    }

    if(type != DEVICE_TYPE_KEYBOARD && type != DEVICE_TYPE_KEYPAD)
    {
        return name;
    }

    if(StartsWithKeyPrefix(name))
    {
        const std::string key_name = Trim(name.substr(4));
        return key_name.empty() ? std::string() : "Key: " + key_name;
    }

    return "Key: " + name;
}

std::vector<QJsonObject> ExtractControlParameters(const QJsonValue& value)
{
    std::vector<QJsonObject> result;
    for(const QJsonValue& item : value.toArray())
    {
        if(!item.isObject())
        {
            continue;
        }

        QJsonObject parameter = item.toObject();
        const QString property = parameter.value("property").toString().trimmed();
        if(property.isEmpty())
        {
            continue;
        }

        parameter.insert("property", property);
        if(parameter.value("label").toString().isEmpty())
        {
            parameter.insert("label", property);
        }
        if(parameter.value("type").toString().isEmpty())
        {
            parameter.insert("type", "text");
        }
        result.push_back(parameter);
    }
    return result;
}

void MergeControlParameters(std::vector<QJsonObject>& target, const QJsonValue& value)
{
    for(const QJsonObject& parameter : ExtractControlParameters(value))
    {
        const QString property = parameter.value("property").toString();
        const auto existing = std::find_if(target.begin(), target.end(), [&property](const QJsonObject& item) {
            return item.value("property").toString() == property;
        });
        if(existing == target.end())
        {
            target.push_back(parameter);
        }
        else
        {
            *existing = parameter;
        }
    }
}

std::vector<int> BuildMatrixMap(
    unsigned int width,
    unsigned int height,
    unsigned int led_count,
    const std::vector<std::pair<int, int>>& positions)
{
    if(width <= 1 || height <= 1)
    {
        return {};
    }

    std::vector<int> map(static_cast<std::size_t>(width) * height, -1);
    if(!positions.empty())
    {
        for(std::size_t idx = 0; idx < positions.size(); idx++)
        {
            const auto [x, y] = positions[idx];
            if(x >= 0 && y >= 0 && static_cast<unsigned int>(x) < width && static_cast<unsigned int>(y) < height)
            {
                map[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = static_cast<int>(idx);
            }
        }
        return map;
    }

    const unsigned int cells = width * height;
    for(unsigned int idx = 0; idx < std::min(cells, led_count); idx++)
    {
        map[idx] = static_cast<int>(idx);
    }
    return map;
}

matrix_map_type* CreateOpenRgbMatrixMap(unsigned int width, unsigned int height, const std::vector<int>& map)
{
    if(width == 0 || height == 0 || map.empty())
    {
        return nullptr;
    }

    auto* matrix = new matrix_map_type;
    matrix->width = width;
    matrix->height = height;
    matrix->map = new unsigned int[static_cast<std::size_t>(width) * height];
    for(std::size_t idx = 0; idx < map.size(); idx++)
    {
        matrix->map[idx] = map[idx] >= 0 ? static_cast<unsigned int>(map[idx]) : kMatrixDisabled;
    }
    return matrix;
}

QJsonArray ByteArrayToJson(const std::vector<unsigned char>& bytes)
{
    QJsonArray array;
    for(unsigned char value : bytes)
    {
        array.append(static_cast<int>(value));
    }
    return array;
}
}

RGBController_SignalBridgeScript::RGBController_SignalBridgeScript(
    std::shared_ptr<SignalBridgeHidBackend> hid_backend,
    SignalBridgeScriptMeta meta,
    SignalBridgeHidInfo primary_hid,
    QJsonObject configuration,
    std::string config_key,
    SignalBridgeScriptLogCallback log_callback)
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
    type = ResolveDeviceType(meta_.device_type);
    flags = CONTROLLER_FLAG_LOCAL;

    mode direct;
    direct.name = "Direct";
    direct.value = 0;
    direct.flags = MODE_FLAG_HAS_PER_LED_COLOR;
    direct.color_mode = MODE_COLORS_PER_LED;
    modes.push_back(direct);
    active_mode = 0;

    OpenEndpoints();
    CreateRuntime();
    InitializeScript();
    SetupColors();
}

RGBController_SignalBridgeScript::~RGBController_SignalBridgeScript()
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
    CloseHandles();
    DeleteZoneMaps();
}

void RGBController_SignalBridgeScript::SetupZones()
{
}

void RGBController_SignalBridgeScript::ResizeZone(int zone_index, int new_size)
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
    RebuildLedList();
    SetupColors();
}

void RGBController_SignalBridgeScript::DeviceUpdateLEDs()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(shutting_down_ || runtime_ == nullptr)
    {
        return;
    }

    try
    {
        QJsonObject main_frame;
        QJsonObject channel_frames;
        QJsonObject subdevice_frames;

        for(std::size_t zone_idx = 0; zone_idx < zones.size() && zone_idx < zone_targets_.size(); zone_idx++)
        {
            const ZoneTarget& target = zone_targets_[zone_idx];
            const QJsonObject frame = BuildFrameForZone(static_cast<unsigned int>(zone_idx), target);
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

        runtime_->SetGlobalJson("__srgb_main_frame", main_frame);
        runtime_->SetGlobalJson("__srgb_channel_frames", channel_frames);
        runtime_->SetGlobalJson("__srgb_subdevice_frames", subdevice_frames);
        runtime_->CallGlobalJson("__srgb_apply_pending_frames");

        if(runtime_->HasModuleExport("Render"))
        {
            runtime_->CallModuleExportJson("Render");
        }
    }
    catch(...)
    {
    }
}

void RGBController_SignalBridgeScript::UpdateZoneLEDs(int)
{
    DeviceUpdateLEDs();
}

void RGBController_SignalBridgeScript::UpdateSingleLED(int)
{
    DeviceUpdateLEDs();
}

void RGBController_SignalBridgeScript::SetCustomMode()
{
    active_mode = 0;
}

void RGBController_SignalBridgeScript::DeviceUpdateMode()
{
}

const std::string& RGBController_SignalBridgeScript::SourcePath() const
{
    return meta_.source_path;
}

const std::string& RGBController_SignalBridgeScript::ConfigKey() const
{
    return config_key_;
}

const SignalBridgeScriptMeta& RGBController_SignalBridgeScript::ScriptMeta() const
{
    return meta_;
}

void RGBController_SignalBridgeScript::SetConfiguration(QJsonObject configuration)
{
    std::lock_guard<std::mutex> lock(mutex_);
    configuration_ = std::move(configuration);
    if(runtime_ != nullptr)
    {
        runtime_->ApplyConfiguration(meta_, configuration_);
    }
}

void RGBController_SignalBridgeScript::SetConfigurationValue(const QString& property, const QJsonValue& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    configuration_.insert(property, value);
    if(runtime_ != nullptr)
    {
        runtime_->ApplyConfiguration(meta_, configuration_);
    }
}

void RGBController_SignalBridgeScript::OpenEndpoints()
{
    primary_handle_ = hid_backend_->OpenPath(primary_hid_.path);
    endpoint_handles_[SignalBridgeHidBackend::EndpointKey(primary_hid_)] = primary_handle_;

    const std::vector<SignalBridgeHidInfo> endpoints = hid_backend_->CollectEndpoints(primary_hid_);
    for(const SignalBridgeHidInfo& endpoint : endpoints)
    {
        const std::string key = SignalBridgeHidBackend::EndpointKey(endpoint);
        endpoints_.push_back({
            endpoint.interface_number.value_or(0),
            endpoint.usage.value_or(0),
            endpoint.usage_page.value_or(0),
        });

        if(endpoint_handles_.find(key) != endpoint_handles_.end())
        {
            continue;
        }

        try
        {
            endpoint_handles_[key] = hid_backend_->OpenPath(endpoint.path);
        }
        catch(...)
        {
        }
    }
}

void RGBController_SignalBridgeScript::CreateRuntime()
{
    runtime_ = std::make_unique<SignalBridgeJsRuntime>(SignalBridgeJsRuntime::CreateRuntime(
        hid_backend_,
        meta_,
        primary_handle_,
        primary_hid_,
        endpoint_handles_,
        endpoints_,
        configuration_,
        log_callback_));
}

void RGBController_SignalBridgeScript::InitializeScript()
{
    if(runtime_->HasModuleExport("Initialize"))
    {
        runtime_->CallModuleExportJson("Initialize");
    }

    try
    {
        MergeControlParameters(meta_.control_parameters, runtime_->CallGlobalJson("__srgb_export_properties"));
        runtime_->ApplyConfiguration(meta_, configuration_);
    }
    catch(...)
    {
    }

    QJsonArray args;
    args.append(true);
    QJsonObject topology = runtime_->CallGlobalJson("__srgb_take_topology_update", args).toObject();
    if(topology.isEmpty())
    {
        QJsonObject main;
        main.insert("width", static_cast<int>(meta_.width));
        main.insert("height", static_cast<int>(meta_.height));
        main.insert("led_count", static_cast<int>(std::max<std::size_t>(meta_.led_names.size(), meta_.led_positions.size())));
        main.insert("canvas_led_count", static_cast<int>(std::max(1u, meta_.width * meta_.height)));
        QJsonArray positions;
        for(const auto& position : meta_.led_positions)
        {
            QJsonArray item;
            item.append(position.first);
            item.append(position.second);
            positions.append(item);
        }
        main.insert("led_positions", positions);
        topology.insert("main", main);
        topology.insert("name", QString::fromStdString(meta_.name));
    }
    BuildZonesFromTopology(topology);
}

void RGBController_SignalBridgeScript::BuildZonesFromTopology(const QJsonObject& topology)
{
    DeleteZoneMaps();
    zones.clear();
    zone_targets_.clear();

    const std::string runtime_name = topology.value("name").toString(QString::fromStdString(meta_.name)).toStdString();
    if(!runtime_name.empty())
    {
        name = runtime_name;
    }

    const QJsonArray channels = topology.value("channels").toArray();
    const QJsonArray subdevices = topology.value("subdevices").toArray();
    const bool has_dynamic_outputs = !channels.isEmpty() || !subdevices.isEmpty();

    const QJsonObject main = topology.value("main").toObject();
    const unsigned int main_width = std::max(1u, JsonUInt(main, "width", meta_.width));
    const unsigned int main_height = std::max(1u, JsonUInt(main, "height", meta_.height));
    const unsigned int main_led_count = JsonUInt(main, "led_count", 0);
    const unsigned int canvas_led_count = JsonUInt(main, "canvas_led_count", main_width * main_height);

    if(main_led_count > 0 || !has_dynamic_outputs)
    {
        const unsigned int led_count = std::max(1u, main_led_count > 0 ? main_led_count : canvas_led_count);
        std::vector<std::pair<int, int>> positions = ExtractPositions(main.value("led_positions"));
        std::vector<std::string> names = ExtractNames(main.value("led_names"));
        if(positions.empty())
        {
            positions = meta_.led_positions;
        }
        if(names.empty())
        {
            names = meta_.led_names;
        }

        ZoneTarget target;
        target.kind = ZoneTarget::Kind::Main;
        target.key = "main";
        target.width = main_width;
        target.height = main_height;
        target.matrix_map = BuildMatrixMap(main_width, main_height, led_count, positions);
        target.led_names = std::move(names);

        zone main_zone;
        main_zone.name = has_dynamic_outputs ? "Main" : name;
        main_zone.type = !target.matrix_map.empty() ? ZONE_TYPE_MATRIX : (led_count <= 1 ? ZONE_TYPE_SINGLE : ZONE_TYPE_LINEAR);
        main_zone.leds_count = led_count;
        main_zone.leds_min = led_count;
        main_zone.leds_max = led_count;
        main_zone.matrix_map = CreateOpenRgbMatrixMap(target.width, target.height, target.matrix_map);
        zones.push_back(main_zone);
        zone_targets_.push_back(std::move(target));
    }

    for(const QJsonValue& value : channels)
    {
        const QJsonObject channel = value.toObject();
        const std::string channel_name = channel.value("name").toString("Channel").toStdString();
        const unsigned int led_count = JsonUInt(channel, "led_count", 0);
        const unsigned int led_limit = JsonUInt(channel, "led_limit", led_count);
        const unsigned int max_leds = std::max(1u, std::max(led_count, led_limit));

        ZoneTarget target;
        target.kind = ZoneTarget::Kind::Channel;
        target.key = channel_name;
        target.width = max_leds;
        target.height = 1;

        zone channel_zone;
        channel_zone.name = channel_name;
        channel_zone.type = max_leds <= 1 ? ZONE_TYPE_SINGLE : ZONE_TYPE_LINEAR;
        channel_zone.leds_count = max_leds;
        channel_zone.leds_min = 0;
        channel_zone.leds_max = max_leds;
        zones.push_back(channel_zone);
        zone_targets_.push_back(std::move(target));
    }

    for(const QJsonValue& value : subdevices)
    {
        const QJsonObject subdevice = value.toObject();
        const std::string subdevice_name = subdevice.value("name").toString("Subdevice").toStdString();
        const std::string display_name = subdevice.value("display_name").toString(QString::fromStdString(subdevice_name)).toStdString();
        const unsigned int sub_width = std::max(1u, JsonUInt(subdevice, "width", 1));
        const unsigned int sub_height = std::max(1u, JsonUInt(subdevice, "height", 1));
        const std::vector<std::pair<int, int>> positions = ExtractPositions(subdevice.value("led_positions"));
        const std::vector<std::string> names = ExtractNames(subdevice.value("led_names"));
        const unsigned int led_count = std::max(
            JsonUInt(subdevice, "led_count", 0),
            static_cast<unsigned int>(std::max(positions.size(), names.size())));

        if(led_count == 0)
        {
            continue;
        }

        ZoneTarget target;
        target.kind = ZoneTarget::Kind::Subdevice;
        target.key = subdevice_name;
        target.width = sub_width;
        target.height = sub_height;
        target.matrix_map = BuildMatrixMap(sub_width, sub_height, led_count, positions);
        target.led_names = names;

        zone sub_zone;
        sub_zone.name = display_name;
        sub_zone.type = !target.matrix_map.empty() ? ZONE_TYPE_MATRIX : (led_count <= 1 ? ZONE_TYPE_SINGLE : ZONE_TYPE_LINEAR);
        sub_zone.leds_count = led_count;
        sub_zone.leds_min = led_count;
        sub_zone.leds_max = led_count;
        sub_zone.matrix_map = CreateOpenRgbMatrixMap(target.width, target.height, target.matrix_map);
        zones.push_back(sub_zone);
        zone_targets_.push_back(std::move(target));
    }

    if(zones.empty())
    {
        ZoneTarget target;
        target.kind = ZoneTarget::Kind::Main;
        target.key = "main";

        zone fallback;
        fallback.name = name;
        fallback.type = ZONE_TYPE_SINGLE;
        fallback.leds_count = 1;
        fallback.leds_min = 1;
        fallback.leds_max = 1;
        zones.push_back(fallback);
        zone_targets_.push_back(std::move(target));
    }

    RebuildLedList();
}

void RGBController_SignalBridgeScript::RebuildLedList()
{
    leds.clear();
    led_alt_names.clear();

    for(std::size_t zone_idx = 0; zone_idx < zones.size(); zone_idx++)
    {
        const zone& current_zone = zones[zone_idx];
        const ZoneTarget& target = zone_targets_[zone_idx];
        for(unsigned int led_idx = 0; led_idx < current_zone.leds_count; led_idx++)
        {
            led item;
            item.value = static_cast<unsigned int>(leds.size());
            if(led_idx < target.led_names.size() && !target.led_names[led_idx].empty())
            {
                item.name = OpenRgbLedName(type, target.led_names[led_idx]);
            }
            else
            {
                item.name = current_zone.name + " " + std::to_string(led_idx + 1);
            }
            leds.push_back(item);
        }
    }
}

void RGBController_SignalBridgeScript::DeleteZoneMaps()
{
    for(zone& current_zone : zones)
    {
        if(current_zone.matrix_map != nullptr)
        {
            delete[] current_zone.matrix_map->map;
            delete current_zone.matrix_map;
            current_zone.matrix_map = nullptr;
        }
    }
}

void RGBController_SignalBridgeScript::CloseHandles()
{
    std::set<SignalBridgeHidBackend::Handle> handles;
    for(const auto& item : endpoint_handles_)
    {
        handles.insert(item.second);
    }
    for(SignalBridgeHidBackend::Handle handle : handles)
    {
        try
        {
            hid_backend_->Close(handle);
        }
        catch(...)
        {
        }
    }
    endpoint_handles_.clear();
    primary_handle_ = 0;
}

QJsonObject RGBController_SignalBridgeScript::BuildFrameForZone(unsigned int zone_index, const ZoneTarget& target) const
{
    const zone& current_zone = zones[zone_index];
    const std::vector<unsigned char> bytes = target.matrix_map.empty()
                                                 ? BuildFlatColors(current_zone.start_idx, current_zone.leds_count)
                                                 : BuildSpatialColors(current_zone.start_idx, target);

    QJsonObject frame;
    frame.insert("colors", ByteArrayToJson(bytes));
    frame.insert("width", static_cast<int>(std::max(1u, target.width)));
    frame.insert("led_count", static_cast<int>(bytes.size() / 3));
    return frame;
}

std::vector<unsigned char> RGBController_SignalBridgeScript::BuildFlatColors(unsigned int start, unsigned int count) const
{
    std::vector<unsigned char> bytes;
    bytes.reserve(static_cast<std::size_t>(count) * 3);
    for(unsigned int idx = 0; idx < count; idx++)
    {
        const RGBColor color = (start + idx) < colors.size() ? colors[start + idx] : 0;
        bytes.push_back(static_cast<unsigned char>(RGBGetRValue(color)));
        bytes.push_back(static_cast<unsigned char>(RGBGetGValue(color)));
        bytes.push_back(static_cast<unsigned char>(RGBGetBValue(color)));
    }
    return bytes;
}

std::vector<unsigned char> RGBController_SignalBridgeScript::BuildSpatialColors(unsigned int start, const ZoneTarget& target) const
{
    std::vector<unsigned char> bytes;
    bytes.reserve(target.matrix_map.size() * 3);
    for(int led_index : target.matrix_map)
    {
        const RGBColor color = led_index >= 0 && (start + static_cast<unsigned int>(led_index)) < colors.size()
                                   ? colors[start + static_cast<unsigned int>(led_index)]
                                   : 0;
        bytes.push_back(static_cast<unsigned char>(RGBGetRValue(color)));
        bytes.push_back(static_cast<unsigned char>(RGBGetGValue(color)));
        bytes.push_back(static_cast<unsigned char>(RGBGetBValue(color)));
    }
    return bytes;
}

device_type RGBController_SignalBridgeScript::ResolveDeviceType(const std::string& signalrgb_type)
{
    const std::string normalized = Lower(signalrgb_type);
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
    if(normalized == "headphones" || normalized == "headset")
    {
        return DEVICE_TYPE_HEADSET;
    }
    if(normalized == "microphone")
    {
        return DEVICE_TYPE_MICROPHONE;
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
    if(normalized == "cooler" || normalized == "aio")
    {
        return DEVICE_TYPE_COOLER;
    }
    if(normalized == "fan")
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
    if(normalized == "accessory" || normalized == "chair")
    {
        return DEVICE_TYPE_ACCESSORY;
    }
    return DEVICE_TYPE_UNKNOWN;
}
