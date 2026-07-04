#include "runtime/signalrgb/SignalRgbModuleRegistry.h"
#include "runtime/signalrgb/SignalRgbModuleUtils.h"

#include "runtime/RuntimeBindingUtils.h"

namespace signalbridge
{
namespace
{
JSValue CreatePermissionsObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "permissions", EmptyArrayJs);
    SetFunctionProperty(context, object, "setCallback", UndefinedJs);
    return object;
}

int PermissionsModuleInit(JSContext* context, JSModuleDef* module)
{
    return ExportObjectModule(context, module, "permissions", "permissions", CreatePermissionsObject);
}

JSModuleDef* LoadPermissionsModule(JSContext* context, const char* module_name)
{
    return LoadObjectModule(context, module_name, PermissionsModuleInit, "permissions");
}

void RegisterPermissionsGlobal(JSContext* context)
{
    SetGlobalProperty(context, "permissions", CreatePermissionsObject(context));
}

[[maybe_unused]] const bool kRegistered = RegisterSignalRgbModule({
    "@SignalRGB/permissions",
    LoadPermissionsModule,
    RegisterPermissionsGlobal,
});
}
}
