#include "openrgb/FrameBuilder.h"

#include <algorithm>

namespace signalbridge
{
namespace
{
void AppendRgb(std::vector<std::uint8_t>& bytes, RGBColor color)
{
    bytes.push_back(static_cast<std::uint8_t>(RGBGetRValue(color)));
    bytes.push_back(static_cast<std::uint8_t>(RGBGetGValue(color)));
    bytes.push_back(static_cast<std::uint8_t>(RGBGetBValue(color)));
}

void BuildFlatColors(std::vector<std::uint8_t>& bytes, const std::vector<RGBColor>& colors, unsigned int start, unsigned int count)
{
    bytes.reserve(static_cast<std::size_t>(count) * 3);
    for(unsigned int idx = 0; idx < count; idx++)
    {
        const RGBColor color = (start + idx) < colors.size() ? colors[start + idx] : 0;
        AppendRgb(bytes, color);
    }
}

void BuildSpatialColors(std::vector<std::uint8_t>& bytes, const std::vector<RGBColor>& colors, unsigned int start, const ZoneTarget& target)
{
    bytes.reserve(target.matrix_map.size() * 3);
    for(int led_index : target.matrix_map)
    {
        const RGBColor color = led_index >= 0 && (start + static_cast<unsigned int>(led_index)) < colors.size()
                                   ? colors[start + static_cast<unsigned int>(led_index)]
                                   : 0;
        AppendRgb(bytes, color);
    }
}
}

std::size_t BuildFrameColorsForZone(
    const std::vector<zone>& zones,
    const std::vector<RGBColor>& colors,
    unsigned int zone_index,
    const ZoneTarget& target,
    std::vector<std::uint8_t>& frame_colors)
{
    const zone& current_zone = zones[zone_index];
    frame_colors.clear();
    if(target.matrix_map.empty())
    {
        BuildFlatColors(frame_colors, colors, current_zone.start_idx, current_zone.leds_count);
    }
    else
    {
        BuildSpatialColors(frame_colors, colors, current_zone.start_idx, target);
    }

    return frame_colors.size() / 3;
}

void BuildFrameForZone(
    const std::vector<zone>& zones,
    const std::vector<RGBColor>& colors,
    unsigned int zone_index,
    const ZoneTarget& target,
    RuntimeColorFrame& frame)
{
    frame.width = static_cast<int>(std::max(1u, target.width));
    frame.led_count = static_cast<int>(BuildFrameColorsForZone(zones, colors, zone_index, target, frame.colors));
}
}
