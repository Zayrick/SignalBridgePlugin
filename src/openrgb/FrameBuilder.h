#ifndef SIGNALBRIDGE_FRAME_BUILDER_H
#define SIGNALBRIDGE_FRAME_BUILDER_H

#include <vector>

#ifndef SIGNALBRIDGE_OPENRGB_API_VERSION
#define SIGNALBRIDGE_OPENRGB_API_VERSION 4
#endif

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
#include "RGBController/RGBControllerInterface.h"
#elif SIGNALBRIDGE_OPENRGB_API_VERSION == 4
#include "RGBController/RGBController.h"
#else
#error Unsupported SIGNALBRIDGE_OPENRGB_API_VERSION
#endif

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
