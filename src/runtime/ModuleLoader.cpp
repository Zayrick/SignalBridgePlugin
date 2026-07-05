#include "runtime/ModuleLoader.h"

#include "domain/PathUtils.h"
#include "runtime/BuiltinModules.h"
#include "runtime/RuntimeBindings.h"

extern "C" {
#include "quickjs.h"
}

namespace signalbridge
{
void ModuleLoaderState::SetCatalog(const std::vector<ScriptSource>& sources)
{
    catalog = sources;
    loaded_modules.clear();
    loaded_keys.clear();
}

const ScriptSource* ModuleLoaderState::Find(const std::string& lookup_path) const
{
    const std::string normalized = NormalizeLookupPath(lookup_path);
    const std::string normalized_with_js = normalized.size() > 3 &&
                                                   normalized.substr(normalized.size() - 3) == ".js"
                                               ? normalized
                                               : normalized + ".js";
    const std::string key = LowerAscii(normalized);
    const std::string key_with_js = LowerAscii(normalized_with_js);
    for(const ScriptSource& source : catalog)
    {
        const std::string candidate = LowerAscii(NormalizeLookupPath(source.lookup_path));
        if(candidate == key || candidate == key_with_js)
        {
            return &source;
        }
    }
    return nullptr;
}

std::string ModuleLoaderState::Resolve(const std::string& base_name, const std::string& module_name) const
{
    if(!module_name.empty() && module_name.front() == '.')
    {
        const std::string candidate = NormalizeLookupPath(JoinLookupPath(LookupDir(base_name), module_name));
        if(const ScriptSource* source = Find(candidate))
        {
            return NormalizeLookupPath(source->lookup_path);
        }
        return candidate;
    }

    if(IsSignalBridgeHostModule(module_name))
    {
        return module_name;
    }

    if(IsBuiltinModule(module_name))
    {
        return NormalizeBuiltinSpecifier(module_name);
    }

    if(const ScriptSource* source = Find(module_name))
    {
        return NormalizeLookupPath(source->lookup_path);
    }
    return module_name;
}

void ModuleLoaderState::RecordLoaded(const ScriptSource& source)
{
    const std::string key = LowerAscii(NormalizeLookupPath(source.lookup_path));
    if(loaded_keys.insert(key).second)
    {
        loaded_modules.push_back(source);
    }
}

namespace
{
JSModuleDef* CompileModule(JSContext* context, const std::string& name, const std::string& source)
{
    JSValue compiled = JS_Eval(
        context,
        source.c_str(),
        source.size(),
        name.c_str(),
        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if(JS_IsException(compiled))
    {
        return nullptr;
    }

    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    JS_FreeValue(context, compiled);
    return module;
}
}

char* NormalizeModuleName(JSContext* context, const char* module_base_name, const char* module_name, void* opaque)
{
    auto* state = static_cast<ModuleLoaderState*>(opaque);
    if(state == nullptr || module_name == nullptr)
    {
        return nullptr;
    }

    const std::string base = module_base_name != nullptr ? module_base_name : "";
    const std::string normalized = state->Resolve(base, module_name);
    if(!module_name[0] || (module_name[0] == '.' && state->Find(normalized) == nullptr))
    {
        JS_ThrowReferenceError(context, "relative import not found: %s from %s", module_name, base.c_str());
        return nullptr;
    }
    return js_strdup(context, normalized.c_str());
}

JSModuleDef* LoadModule(JSContext* context, const char* module_name, void* opaque)
{
    auto* state = static_cast<ModuleLoaderState*>(opaque);
    if(state == nullptr || module_name == nullptr)
    {
        JS_ThrowReferenceError(context, "module loader is not initialized");
        return nullptr;
    }

    const std::string name = module_name;
    if(IsSignalBridgeHostModule(name))
    {
        return LoadSignalBridgeHostModule(context, module_name);
    }

    if(IsBuiltinModule(name))
    {
        return LoadBuiltinModule(context, name);
    }

    const ScriptSource* source = state->Find(name);
    if(source == nullptr)
    {
        JS_ThrowReferenceError(context, "could not load module '%s'", module_name);
        return nullptr;
    }

    state->RecordLoaded(*source);
    return CompileModule(context, NormalizeLookupPath(source->lookup_path), source->source);
}
}
