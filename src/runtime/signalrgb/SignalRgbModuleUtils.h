#ifndef SIGNALBRIDGE_SIGNALRGB_MODULE_UTILS_H
#define SIGNALBRIDGE_SIGNALRGB_MODULE_UTILS_H

extern "C" {
#include "quickjs.h"
}

namespace signalbridge
{
using SignalRgbObjectFactory = JSValue (*)(JSContext*);

int ExportObjectModule(
    JSContext* context,
    JSModuleDef* module,
    const char* global_name,
    const char* named_export,
    SignalRgbObjectFactory factory);

JSModuleDef* LoadObjectModule(
    JSContext* context,
    const char* module_name,
    JSModuleInitFunc* initializer,
    const char* named_export);
}

#endif // SIGNALBRIDGE_SIGNALRGB_MODULE_UTILS_H
