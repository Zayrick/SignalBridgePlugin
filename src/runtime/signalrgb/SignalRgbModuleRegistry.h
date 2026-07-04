#ifndef SIGNALBRIDGE_SIGNALRGB_MODULE_REGISTRY_H
#define SIGNALBRIDGE_SIGNALRGB_MODULE_REGISTRY_H

struct JSContext;
struct JSModuleDef;

namespace signalbridge
{
using SignalRgbModuleLoader = JSModuleDef* (*)(JSContext*, const char*);
using SignalRgbGlobalRegistrar = void (*)(JSContext*);

struct SignalRgbModuleRegistration
{
    const char* specifier;
    SignalRgbModuleLoader load;
    SignalRgbGlobalRegistrar register_globals = nullptr;
};

bool RegisterSignalRgbModule(SignalRgbModuleRegistration registration);
JSModuleDef* LoadRegisteredSignalRgbModule(JSContext* context, const char* specifier);
void RegisterSignalRgbPackageGlobals(JSContext* context);
}

#endif // SIGNALBRIDGE_SIGNALRGB_MODULE_REGISTRY_H
