#include "runtime/signalrgb/SignalRgbModuleRegistry.h"
#include "runtime/signalrgb/SignalRgbModuleUtils.h"

#include "runtime/RuntimeBindingUtils.h"

namespace signalbridge
{
namespace
{
JSValue CreateDeviceDiscoveryObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "foundVirtualDevice", UndefinedJs);
    return object;
}

int DeviceDiscoveryModuleInit(JSContext* context, JSModuleDef* module)
{
    return ExportObjectModule(context, module, "DeviceDiscovery", "DeviceDiscovery", CreateDeviceDiscoveryObject);
}

JSModuleDef* LoadDeviceDiscoveryModule(JSContext* context, const char* module_name)
{
    return LoadObjectModule(context, module_name, DeviceDiscoveryModuleInit, "DeviceDiscovery");
}

void RegisterDeviceDiscoveryGlobal(JSContext* context)
{
    SetGlobalProperty(context, "DeviceDiscovery", CreateDeviceDiscoveryObject(context));
}

[[maybe_unused]] const bool kRegistered = RegisterSignalRgbModule({
    "@SignalRGB/DeviceDiscovery",
    LoadDeviceDiscoveryModule,
    RegisterDeviceDiscoveryGlobal,
});
}
}
