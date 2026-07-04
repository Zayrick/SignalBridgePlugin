#include "runtime/signalrgb/SignalRgbEnvironmentGlobals.h"

#include "runtime/RuntimeBindingUtils.h"

namespace signalbridge
{
namespace
{
JSValue CreateBatteryObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "setBatteryLevel", UndefinedJs);
    SetFunctionProperty(context, object, "setBatteryState", UndefinedJs);
    return object;
}

JSValue CreateKeyboardObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "sendEvent", UndefinedJs);
    SetFunctionProperty(context, object, "sendHid", UndefinedJs);
    return object;
}

JSValue CreateMouseObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "sendEvent", UndefinedJs);
    return object;
}
}

void RegisterSignalRgbEnvironmentGlobals(JSContext* context)
{
    SetGlobalProperty(context, "battery", CreateBatteryObject(context));
    SetGlobalProperty(context, "keyboard", CreateKeyboardObject(context));
    SetGlobalProperty(context, "mouse", CreateMouseObject(context));
    SetGlobalProperty(context, "LightingMode", JS_NewString(context, "Canvas"));
    SetGlobalProperty(context, "forcedColor", JS_NewString(context, "#000000"));
}
}
