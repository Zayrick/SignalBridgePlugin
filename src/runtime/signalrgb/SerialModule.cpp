#include "runtime/signalrgb/SignalRgbModuleRegistry.h"
#include "runtime/signalrgb/SignalRgbModuleUtils.h"

#include "runtime/RuntimeBindingUtils.h"

namespace signalbridge
{
namespace
{
JSValue EmptyObjectJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JS_NewObject(context);
}

JSValue CreateSerialObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "availablePorts", EmptyArrayJs);
    SetFunctionProperty(context, object, "getDeviceInfo", EmptyObjectJs);
    SetFunctionProperty(context, object, "connect", FalseJs);
    SetFunctionProperty(context, object, "disconnect", UndefinedJs);
    SetFunctionProperty(context, object, "isConnected", FalseJs);
    SetFunctionProperty(context, object, "write", FalseJs);
    SetFunctionProperty(context, object, "read", EmptyArrayJs);
    return object;
}

int SerialModuleInit(JSContext* context, JSModuleDef* module)
{
    return ExportObjectModule(context, module, nullptr, "serial", CreateSerialObject);
}

JSModuleDef* LoadSerialModule(JSContext* context, const char* module_name)
{
    return LoadObjectModule(context, module_name, SerialModuleInit, "serial");
}

[[maybe_unused]] const bool kRegistered = RegisterSignalRgbModule({
    "@SignalRGB/serial",
    LoadSerialModule,
    nullptr,
});
}
}
