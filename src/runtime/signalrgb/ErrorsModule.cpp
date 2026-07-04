#include "runtime/QuickJsValue.h"
#include "runtime/RuntimeBindingUtils.h"
#include "runtime/RuntimeBindings.h"
#include "runtime/signalrgb/SignalRgbModuleRegistry.h"

namespace signalbridge
{
namespace
{
std::string AssertMessage(JSContext* context, int argc, JSValueConst* argv, int index, const char* fallback)
{
    return argc > index ? JsToString(context, argv[index], fallback) : std::string(fallback);
}

JSValue ContextErrorJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    JSValue object = JS_NewObject(context);
    SetValueProperty(context, object, "message", JS_NewString(context, AssertMessage(context, argc, argv, 0, "").c_str()));
    SetValueProperty(context, object, "name", JS_NewString(context, "ContextError"));
    return object;
}

JSValue AssertIsOkJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if(argc < 1 || JS_ToBool(context, argv[0]) == 0)
    {
        return JS_ThrowInternalError(context, "%s", AssertMessage(context, argc, argv, 1, "Assertion failed").c_str());
    }
    return JS_UNDEFINED;
}

JSValue AssertFailJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    return JS_ThrowInternalError(context, "%s", AssertMessage(context, argc, argv, 0, "Assertion failed").c_str());
}

JSValue AssertUnreachableJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    return JS_ThrowInternalError(context, "%s", AssertMessage(context, argc, argv, 0, "Unreachable").c_str());
}

JSValue AssertIsEqualJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if(argc < 2 || !JS_IsSameValue(context, argv[0], argv[1]))
    {
        return JS_ThrowInternalError(context, "%s", AssertMessage(context, argc, argv, 2, "Assertion failed").c_str());
    }
    return JS_UNDEFINED;
}

JSValue AssertSoftIsDefinedJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    return JS_NewBool(context, argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]));
}

JSValue GlobalContextSetJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc >= 2)
    {
        state->global_context[JsToString(context, argv[0])] = JsValueToJson(context, argv[1]);
    }
    return JS_UNDEFINED;
}

JSValue GlobalContextGetJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || argc < 1)
    {
        return JS_UNDEFINED;
    }
    const auto it = state->global_context.find(JsToString(context, argv[0]));
    return it != state->global_context.end() ? JsonToJsValue(context, it->second, "<globalContext>") : JS_UNDEFINED;
}

JSValue GlobalContextHasJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    return JS_NewBool(context, state != nullptr && argc >= 1 && state->global_context.count(JsToString(context, argv[0])) > 0);
}

JSValue GlobalContextClearJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc >= 1)
    {
        state->global_context.erase(JsToString(context, argv[0]));
    }
    return JS_UNDEFINED;
}

JSValue CreateContextErrorFunction(JSContext* context)
{
    return JS_NewCFunction2(context, ContextErrorJs, "ContextError", 1, JS_CFUNC_constructor_or_func, 0);
}

JSValue CreateAssertObject(JSContext* context)
{
    JSValue assert = JS_NewObject(context);
    SetFunctionProperty(context, assert, "isOk", AssertIsOkJs, 2);
    SetFunctionProperty(context, assert, "fail", AssertFailJs, 1);
    SetFunctionProperty(context, assert, "unreachable", AssertUnreachableJs, 1);
    SetFunctionProperty(context, assert, "isEqual", AssertIsEqualJs, 3);
    SetFunctionProperty(context, assert, "softIsDefined", AssertSoftIsDefinedJs, 1);
    return assert;
}

JSValue CreateGlobalContextObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "set", GlobalContextSetJs, 2);
    SetFunctionProperty(context, object, "get", GlobalContextGetJs, 1);
    SetFunctionProperty(context, object, "has", GlobalContextHasJs, 1);
    SetFunctionProperty(context, object, "clear", GlobalContextClearJs, 1);
    return object;
}

int ErrorsModuleInit(JSContext* context, JSModuleDef* module)
{
    JSValue context_error = GetOrCreateGlobalProperty(context, "ContextError", CreateContextErrorFunction);
    JSValue assert = GetOrCreateGlobalProperty(context, "Assert", CreateAssertObject);
    JSValue global_context = GetOrCreateGlobalProperty(context, "globalContext", CreateGlobalContextObject);

    JSValue default_object = JS_NewObject(context);
    SetValueProperty(context, default_object, "Assert", JS_DupValue(context, assert));
    SetValueProperty(context, default_object, "ContextError", JS_DupValue(context, context_error));
    SetValueProperty(context, default_object, "globalContext", JS_DupValue(context, global_context));

    if(JS_SetModuleExport(context, module, "Assert", assert) < 0)
    {
        JS_FreeValue(context, assert);
        JS_FreeValue(context, context_error);
        JS_FreeValue(context, global_context);
        JS_FreeValue(context, default_object);
        return -1;
    }
    if(JS_SetModuleExport(context, module, "ContextError", context_error) < 0)
    {
        JS_FreeValue(context, context_error);
        JS_FreeValue(context, global_context);
        JS_FreeValue(context, default_object);
        return -1;
    }
    if(JS_SetModuleExport(context, module, "globalContext", global_context) < 0)
    {
        JS_FreeValue(context, global_context);
        JS_FreeValue(context, default_object);
        return -1;
    }
    if(JS_SetModuleExport(context, module, "default", default_object) < 0)
    {
        JS_FreeValue(context, default_object);
        return -1;
    }
    return 0;
}

void RegisterErrorGlobals(JSContext* context)
{
    SetGlobalProperty(context, "ContextError", CreateContextErrorFunction(context));
    SetGlobalProperty(context, "Assert", CreateAssertObject(context));
    SetGlobalProperty(context, "globalContext", CreateGlobalContextObject(context));
}

JSModuleDef* LoadErrorsModule(JSContext* context, const char* module_name)
{
    JSModuleDef* module = JS_NewCModule(context, module_name, ErrorsModuleInit);
    if(module == nullptr)
    {
        return nullptr;
    }
    if(JS_AddModuleExport(context, module, "Assert") < 0 ||
       JS_AddModuleExport(context, module, "ContextError") < 0 ||
       JS_AddModuleExport(context, module, "globalContext") < 0 ||
       JS_AddModuleExport(context, module, "default") < 0)
    {
        return nullptr;
    }
    return module;
}

[[maybe_unused]] const bool kRegistered = RegisterSignalRgbModule({
    "@SignalRGB/Errors",
    LoadErrorsModule,
    RegisterErrorGlobals,
});
}
}
