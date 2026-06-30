#include "openrgb/TopologyMapper.h"

#include <algorithm>
#include <cctype>

#include <QJsonArray>
#include <QJsonValue>

namespace signalbridge
{
namespace
{
constexpr unsigned int kMatrixDisabled = 0xFFFFFFFF;

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

QJsonObject FallbackTopology(const ScriptMeta& meta)
{
    QJsonObject topology;
    QJsonObject main;
    main.insert("width", static_cast<int>(meta.width));
    main.insert("height", static_cast<int>(meta.height));
    main.insert("led_count", static_cast<int>(std::max(meta.led_names.size(), meta.led_positions.size())));
    main.insert("canvas_led_count", static_cast<int>(std::max(1u, meta.width * meta.height)));
    QJsonArray positions;
    for(const auto& position : meta.led_positions)
    {
        QJsonArray item;
        item.append(position.first);
        item.append(position.second);
        positions.append(item);
    }
    main.insert("led_positions", positions);
    topology.insert("main", main);
    topology.insert("name", QString::fromStdString(meta.name));
    return topology;
}
}

TopologyResult BuildOpenRgbTopology(const QJsonObject& raw_topology, const ScriptMeta& meta, const std::string& current_name)
{
    TopologyResult result;
    const QJsonObject topology = raw_topology.isEmpty() ? FallbackTopology(meta) : raw_topology;

    result.controller_name = topology.value("name").toString(QString::fromStdString(meta.name)).toStdString();
    if(result.controller_name.empty())
    {
        result.controller_name = current_name;
    }

    const QJsonArray channels = topology.value("channels").toArray();
    const QJsonArray subdevices = topology.value("subdevices").toArray();
    const bool has_dynamic_outputs = !channels.isEmpty() || !subdevices.isEmpty();

    const QJsonObject main = topology.value("main").toObject();
    const unsigned int main_width = std::max(1u, JsonUInt(main, "width", meta.width));
    const unsigned int main_height = std::max(1u, JsonUInt(main, "height", meta.height));
    const unsigned int main_led_count = JsonUInt(main, "led_count", 0);
    const unsigned int canvas_led_count = JsonUInt(main, "canvas_led_count", main_width * main_height);

    if(main_led_count > 0 || !has_dynamic_outputs)
    {
        const unsigned int led_count = std::max(1u, main_led_count > 0 ? main_led_count : canvas_led_count);
        std::vector<std::pair<int, int>> positions = ExtractPositions(main.value("led_positions"));
        std::vector<std::string> names = ExtractNames(main.value("led_names"));
        if(positions.empty())
        {
            positions = meta.led_positions;
        }
        if(names.empty())
        {
            names = meta.led_names;
        }

        ZoneTarget target;
        target.kind = ZoneTarget::Kind::Main;
        target.key = "main";
        target.width = main_width;
        target.height = main_height;
        target.matrix_map = BuildMatrixMap(main_width, main_height, led_count, positions);
        target.led_names = std::move(names);

        zone main_zone;
        main_zone.name = has_dynamic_outputs ? "Main" : result.controller_name;
        main_zone.type = !target.matrix_map.empty() ? ZONE_TYPE_MATRIX : (led_count <= 1 ? ZONE_TYPE_SINGLE : ZONE_TYPE_LINEAR);
        main_zone.leds_count = led_count;
        main_zone.leds_min = led_count;
        main_zone.leds_max = led_count;
        main_zone.matrix_map = CreateOpenRgbMatrixMap(target.width, target.height, target.matrix_map);
        result.zones.push_back(main_zone);
        result.targets.push_back(std::move(target));
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
        result.zones.push_back(channel_zone);
        result.targets.push_back(std::move(target));
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
        result.zones.push_back(sub_zone);
        result.targets.push_back(std::move(target));
    }

    if(result.zones.empty())
    {
        ZoneTarget target;
        target.kind = ZoneTarget::Kind::Main;
        target.key = "main";

        zone fallback;
        fallback.name = result.controller_name;
        fallback.type = ZONE_TYPE_SINGLE;
        fallback.leds_count = 1;
        fallback.leds_min = 1;
        fallback.leds_max = 1;
        result.zones.push_back(fallback);
        result.targets.push_back(std::move(target));
    }

    return result;
}

void DeleteZoneMaps(std::vector<zone>& zones)
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

void RebuildOpenRgbLedList(
    const std::vector<zone>& zones,
    const std::vector<ZoneTarget>& targets,
    device_type type,
    std::vector<led>& leds,
    std::vector<std::string>& led_alt_names)
{
    leds.clear();
    led_alt_names.clear();

    for(std::size_t zone_idx = 0; zone_idx < zones.size(); zone_idx++)
    {
        const zone& current_zone = zones[zone_idx];
        const ZoneTarget& target = targets[zone_idx];
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
}
