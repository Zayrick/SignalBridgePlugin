#ifndef SIGNALBRIDGE_BUILTIN_MODULES_H
#define SIGNALBRIDGE_BUILTIN_MODULES_H

#include <string>

struct JSContext;
struct JSModuleDef;

namespace signalbridge
{
std::string NormalizeBuiltinSpecifier(std::string specifier);
bool IsBuiltinModule(const std::string& specifier);
JSModuleDef* LoadBuiltinModule(JSContext* context, const std::string& specifier);
}

#endif // SIGNALBRIDGE_BUILTIN_MODULES_H
