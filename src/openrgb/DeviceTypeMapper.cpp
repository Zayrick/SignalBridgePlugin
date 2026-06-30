#include "openrgb/DeviceTypeMapper.h"

#include "domain/PathUtils.h"

namespace signalbridge
{
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
    if(normalized == "accessory" || normalized == "chair")
    {
        return DEVICE_TYPE_ACCESSORY;
    }
    return DEVICE_TYPE_UNKNOWN;
}
}
