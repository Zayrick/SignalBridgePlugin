#include "runtime/BuiltinModules.h"

#include "runtime/signalrgb/SignalRgbModuleRegistry.h"

extern "C" {
#include "quickjs.h"
}

namespace signalbridge
{
namespace
{
JSModuleDef* ThrowUnknownBuiltin(JSContext* context, const std::string& specifier)
{
    JS_ThrowReferenceError(context, "unknown SignalRGB builtin module '%s'", specifier.c_str());
    return nullptr;
}
}

std::string NormalizeBuiltinSpecifier(std::string specifier)
{
    if(specifier.size() > 3 && specifier.substr(specifier.size() - 3) == ".js")
    {
        specifier.resize(specifier.size() - 3);
    }
    return specifier;
}

bool IsBuiltinModule(const std::string& specifier)
{
    const std::string normalized = NormalizeBuiltinSpecifier(specifier);
    return normalized.rfind("@SignalRGB/", 0) == 0;
}

JSModuleDef* LoadBuiltinModule(JSContext* context, const std::string& specifier)
{
    const std::string normalized = NormalizeBuiltinSpecifier(specifier);
    if(!IsBuiltinModule(normalized))
    {
        return ThrowUnknownBuiltin(context, specifier);
    }
    return LoadRegisteredSignalRgbModule(context, normalized.c_str());
}
}
