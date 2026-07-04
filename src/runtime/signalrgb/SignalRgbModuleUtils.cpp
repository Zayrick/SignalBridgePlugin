#include "runtime/signalrgb/SignalRgbModuleUtils.h"

#include "runtime/RuntimeBindingUtils.h"

namespace signalbridge
{
int ExportObjectModule(
    JSContext* context,
    JSModuleDef* module,
    const char* global_name,
    const char* named_export,
    SignalRgbObjectFactory factory)
{
    JSValue object = global_name != nullptr
                         ? GetOrCreateGlobalProperty(context, global_name, factory)
                         : factory(context);
    JSValue named = JS_DupValue(context, object);
    if(JS_SetModuleExport(context, module, named_export, named) < 0)
    {
        JS_FreeValue(context, named);
        JS_FreeValue(context, object);
        return -1;
    }
    if(JS_SetModuleExport(context, module, "default", object) < 0)
    {
        JS_FreeValue(context, object);
        return -1;
    }
    return 0;
}

JSModuleDef* LoadObjectModule(
    JSContext* context,
    const char* module_name,
    JSModuleInitFunc* initializer,
    const char* named_export)
{
    JSModuleDef* module = JS_NewCModule(context, module_name, initializer);
    if(module == nullptr)
    {
        return nullptr;
    }
    if(JS_AddModuleExport(context, module, named_export) < 0 ||
       JS_AddModuleExport(context, module, "default") < 0)
    {
        return nullptr;
    }
    return module;
}
}
