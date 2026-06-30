#ifndef SIGNALBRIDGE_BUILTIN_MODULES_H
#define SIGNALBRIDGE_BUILTIN_MODULES_H

#include <string>

namespace signalbridge
{
std::string NormalizeBuiltinSpecifier(std::string specifier);
const char* BuiltinModuleSource(const std::string& specifier);
std::string LoadRuntimeResourceText(const std::string& relative_path);
}

#endif // SIGNALBRIDGE_BUILTIN_MODULES_H
