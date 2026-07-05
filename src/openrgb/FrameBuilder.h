#ifndef SIGNALBRIDGE_FRAME_BUILDER_H
#define SIGNALBRIDGE_FRAME_BUILDER_H

#include <vector>

#include "RGBController/RGBController.h"
#include "domain/ColorFrame.h"
#include "openrgb/TopologyMapper.h"

namespace signalbridge
{
std::size_t BuildFrameColorsForZone(
    const std::vector<zone>& zones,
    const std::vector<RGBColor>& colors,
    unsigned int zone_index,
    const ZoneTarget& target,
    std::vector<std::uint8_t>& frame_colors);

void BuildFrameForZone(
    const std::vector<zone>& zones,
    const std::vector<RGBColor>& colors,
    unsigned int zone_index,
    const ZoneTarget& target,
    RuntimeColorFrame& frame);
}

#endif // SIGNALBRIDGE_FRAME_BUILDER_H
