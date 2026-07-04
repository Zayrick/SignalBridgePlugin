#ifndef SIGNALBRIDGE_RUNTIME_BINDING_UTILS_H
#define SIGNALBRIDGE_RUNTIME_BINDING_UTILS_H

#include <string>

extern "C" {
#include "quickjs.h"
}

namespace signalbridge
{
void SetFunctionProperty(JSContext* context, JSValue object, const char* name, JSCFunction* function, int argc = 0);
void SetValueProperty(JSContext* context, JSValue object, const char* name, JSValue value);
void SetGlobalProperty(JSContext* context, const char* name, JSValue value);
JSValue GetGlobalProperty(JSContext* context, const char* name);
JSValue GetOrCreateGlobalProperty(JSContext* context, const char* name, JSValue (*factory)(JSContext*));

std::string JsToString(JSContext* context, JSValueConst value, const std::string& fallback = {});

JSValue EmptyArrayJs(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue UndefinedJs(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue ZeroJs(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue HundredJs(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue FalseJs(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv);
}

#endif // SIGNALBRIDGE_RUNTIME_BINDING_UTILS_H
