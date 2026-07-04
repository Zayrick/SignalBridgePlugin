#include "runtime/signalrgb/SignalRgbModuleRegistry.h"

#include <string>
#include <vector>

extern "C" {
#include "quickjs.h"
}

namespace signalbridge
{
namespace
{
std::vector<SignalRgbModuleRegistration>& Registry()
{
    static std::vector<SignalRgbModuleRegistration> modules;
    return modules;
}
}

bool RegisterSignalRgbModule(SignalRgbModuleRegistration registration)
{
    if(registration.specifier == nullptr || registration.load == nullptr)
    {
        return false;
    }

    std::vector<SignalRgbModuleRegistration>& modules = Registry();
    const std::string specifier = registration.specifier;
    for(SignalRgbModuleRegistration& existing : modules)
    {
        if(existing.specifier != nullptr && specifier == existing.specifier)
        {
            existing = registration;
            return true;
        }
    }

    modules.push_back(registration);
    return true;
}

JSModuleDef* LoadRegisteredSignalRgbModule(JSContext* context, const char* specifier)
{
    const std::string requested = specifier != nullptr ? specifier : "";
    for(const SignalRgbModuleRegistration& registration : Registry())
    {
        if(registration.specifier != nullptr && requested == registration.specifier)
        {
            return registration.load(context, specifier);
        }
    }

    JS_ThrowReferenceError(context, "unknown SignalRGB builtin module '%s'", requested.c_str());
    return nullptr;
}

void RegisterSignalRgbPackageGlobals(JSContext* context)
{
    for(const SignalRgbModuleRegistration& registration : Registry())
    {
        if(registration.register_globals != nullptr)
        {
            registration.register_globals(context);
        }
    }
}
}
