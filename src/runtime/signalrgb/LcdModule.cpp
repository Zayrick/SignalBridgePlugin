#include "runtime/signalrgb/SignalRgbModuleRegistry.h"
#include "runtime/signalrgb/SignalRgbModuleUtils.h"

#include "runtime/RuntimeBindingUtils.h"

namespace signalbridge
{
namespace
{
JSValue CreateLcdObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "initialize", UndefinedJs);
    SetFunctionProperty(context, object, "getFrame", EmptyArrayJs);
    return object;
}

int LcdModuleInit(JSContext* context, JSModuleDef* module)
{
    return ExportObjectModule(context, module, "LCD", "LCD", CreateLcdObject);
}

JSModuleDef* LoadLcdModule(JSContext* context, const char* module_name)
{
    return LoadObjectModule(context, module_name, LcdModuleInit, "LCD");
}

void RegisterLcdGlobal(JSContext* context)
{
    SetGlobalProperty(context, "LCD", CreateLcdObject(context));
}

[[maybe_unused]] const bool kRegistered = RegisterSignalRgbModule({
    "@SignalRGB/lcd",
    LoadLcdModule,
    RegisterLcdGlobal,
});
}
}
