#include "openrgb/FrameBuilder.h"

#include <algorithm>

#include <QJsonArray>

namespace signalbridge
{
namespace
{
QJsonArray ByteArrayToJson(const std::vector<unsigned char>& bytes)
{
    QJsonArray array;
    for(unsigned char value : bytes)
    {
        array.append(static_cast<int>(value));
    }
    return array;
}

std::vector<unsigned char> BuildFlatColors(const std::vector<RGBColor>& colors, unsigned int start, unsigned int count)
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

std::vector<unsigned char> BuildSpatialColors(const std::vector<RGBColor>& colors, unsigned int start, const ZoneTarget& target)
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
}

QJsonObject BuildFrameForZone(
    const std::vector<zone>& zones,
    const std::vector<RGBColor>& colors,
    unsigned int zone_index,
    const ZoneTarget& target)
{
    const zone& current_zone = zones[zone_index];
    const std::vector<unsigned char> bytes = target.matrix_map.empty()
                                                 ? BuildFlatColors(colors, current_zone.start_idx, current_zone.leds_count)
                                                 : BuildSpatialColors(colors, current_zone.start_idx, target);

    QJsonObject frame;
    frame.insert("colors", ByteArrayToJson(bytes));
    frame.insert("width", static_cast<int>(std::max(1u, target.width)));
    frame.insert("led_count", static_cast<int>(bytes.size() / 3));
    return frame;
}
}
