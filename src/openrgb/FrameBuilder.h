#ifndef SIGNALBRIDGE_FRAME_BUILDER_H
#define SIGNALBRIDGE_FRAME_BUILDER_H

#include <vector>

#include <QJsonObject>

#include "RGBController/RGBController.h"
#include "openrgb/TopologyMapper.h"

namespace signalbridge
{
QJsonObject BuildFrameForZone(
    const std::vector<zone>& zones,
    const std::vector<RGBColor>& colors,
    unsigned int zone_index,
    const ZoneTarget& target);
}

#endif // SIGNALBRIDGE_FRAME_BUILDER_H
