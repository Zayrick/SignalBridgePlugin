#ifndef SIGNALBRIDGE_DEVICE_TYPE_MAPPER_H
#define SIGNALBRIDGE_DEVICE_TYPE_MAPPER_H

#include <string>

#include "RGBController/RGBController.h"

namespace signalbridge
{
device_type ResolveOpenRgbDeviceType(const std::string& signalrgb_type);
}

#endif // SIGNALBRIDGE_DEVICE_TYPE_MAPPER_H
