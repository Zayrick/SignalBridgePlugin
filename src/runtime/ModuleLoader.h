#ifndef SIGNALBRIDGE_MODULE_LOADER_H
#define SIGNALBRIDGE_MODULE_LOADER_H

#include <set>
#include <string>
#include <vector>

#include "domain/ScriptTypes.h"

struct JSContext;
struct JSModuleDef;

namespace signalbridge
{
struct ModuleLoaderState
{
    std::vector<ScriptSource> catalog;
    std::vector<ScriptSource> loaded_modules;
    std::set<std::string> loaded_keys;

    void SetCatalog(const std::vector<ScriptSource>& sources);
    void BeginLoad();
    const ScriptSource* Find(const std::string& lookup_path) const;
    std::string Resolve(const std::string& base_name, const std::string& module_name) const;
    void RecordLoaded(const ScriptSource& source);
};

JSModuleDef* CompileModule(JSContext* context, const std::string& name, const std::string& source);
char* NormalizeModuleName(JSContext* context, const char* module_base_name, const char* module_name, void* opaque);
JSModuleDef* LoadModule(JSContext* context, const char* module_name, void* opaque);
}

#endif // SIGNALBRIDGE_MODULE_LOADER_H
