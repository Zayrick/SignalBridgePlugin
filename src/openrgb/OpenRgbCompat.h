#ifndef SIGNALBRIDGE_OPENRGB_COMPAT_H
#define SIGNALBRIDGE_OPENRGB_COMPAT_H

#include <vector>

#ifndef SIGNALBRIDGE_OPENRGB_API_VERSION
#define SIGNALBRIDGE_OPENRGB_API_VERSION 4
#endif

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
#include "OpenRGBPluginInterface.h"
#else
#include "ResourceManagerInterface.h"
#include "RGBController/RGBController.h"
#endif

namespace signalbridge
{
#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
using OpenRgbHostInterface = OpenRGBPluginAPIInterface;
using OpenRgbControllerInterface = RGBControllerInterface;

inline void WaitForOpenRgbDetection(OpenRgbHostInterface* host)
{
    if(host != nullptr)
    {
        host->WaitForDetection();
    }
}

inline std::vector<OpenRgbControllerInterface*> OpenRgbControllers(OpenRgbHostInterface* host)
{
    return host != nullptr ? host->GetRGBControllers() : std::vector<OpenRgbControllerInterface*>();
}
#elif SIGNALBRIDGE_OPENRGB_API_VERSION == 4
using OpenRgbHostInterface = ResourceManagerInterface;
using OpenRgbControllerInterface = RGBController;

inline void WaitForOpenRgbDetection(OpenRgbHostInterface* host)
{
    if(host != nullptr)
    {
        host->WaitForDeviceDetection();
    }
}

inline std::vector<OpenRgbControllerInterface*> OpenRgbControllers(OpenRgbHostInterface* host)
{
    if(host == nullptr)
    {
        return {};
    }
    const std::vector<RGBController*>& controllers = host->GetRGBControllers();
    return { controllers.begin(), controllers.end() };
}
#else
#error Unsupported SIGNALBRIDGE_OPENRGB_API_VERSION
#endif
}

#endif // SIGNALBRIDGE_OPENRGB_COMPAT_H
