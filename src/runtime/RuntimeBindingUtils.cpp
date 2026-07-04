#include "runtime/RuntimeBindingUtils.h"

#include <stdexcept>

namespace signalbridge
{
void SetFunctionProperty(JSContext* context, JSValue object, const char* name, JSCFunction* function, int argc)
{
    JSValue js_function = JS_NewCFunction(context, function, name, argc);
    if(JS_SetPropertyStr(context, object, name, js_function) < 0)
    {
        throw std::runtime_error(std::string("failed to register function ") + name);
    }
}

void SetValueProperty(JSContext* context, JSValue object, const char* name, JSValue value)
{
    if(JS_SetPropertyStr(context, object, name, value) < 0)
    {
        throw std::runtime_error(std::string("failed to set property ") + name);
    }
}

void SetGlobalProperty(JSContext* context, const char* name, JSValue value)
{
    JSValue global = JS_GetGlobalObject(context);
    if(JS_SetPropertyStr(context, global, name, value) < 0)
    {
        JS_FreeValue(context, global);
        throw std::runtime_error(std::string("failed to set global ") + name);
    }
    JS_FreeValue(context, global);
}

JSValue GetGlobalProperty(JSContext* context, const char* name)
{
    JSValue global = JS_GetGlobalObject(context);
    JSValue value = JS_GetPropertyStr(context, global, name);
    JS_FreeValue(context, global);
    return value;
}

JSValue GetOrCreateGlobalProperty(JSContext* context, const char* name, JSValue (*factory)(JSContext*))
{
    JSValue value = GetGlobalProperty(context, name);
    if(!JS_IsException(value) && !JS_IsUndefined(value))
    {
        return value;
    }
    JS_FreeValue(context, value);

    value = factory(context);
    SetGlobalProperty(context, name, JS_DupValue(context, value));
    return value;
}

std::string JsToString(JSContext* context, JSValueConst value, const std::string& fallback)
{
    if(JS_IsUndefined(value) || JS_IsNull(value))
    {
        return fallback;
    }
    const char* text = JS_ToCString(context, value);
    std::string result = text != nullptr ? text : fallback;
    if(text != nullptr)
    {
        JS_FreeCString(context, text);
    }
    return result;
}

JSValue EmptyArrayJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JS_NewArray(context);
}

JSValue UndefinedJs(JSContext*, JSValueConst, int, JSValueConst*)
{
    return JS_UNDEFINED;
}

JSValue ZeroJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JS_NewInt32(context, 0);
}

JSValue HundredJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JS_NewInt32(context, 100);
}

JSValue FalseJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JS_NewBool(context, false);
}
}
