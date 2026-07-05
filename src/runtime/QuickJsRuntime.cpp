#include "runtime/QuickJsRuntime.h"

#include <stdexcept>
#include <utility>

#include <QByteArray>
#include <QJsonArray>

#include "domain/ControlParameters.h"
#include "domain/PathUtils.h"
#include "runtime/ModuleLoader.h"
#include "runtime/QuickJsValue.h"

extern "C" {
#include "quickjs.h"
}

namespace signalbridge
{
namespace
{
std::string ConfigurationCallbackName(const QString& property)
{
    const QByteArray property_bytes = property.toUtf8();
    return std::string("on") + property_bytes.constData() + "Changed";
}
}

struct RuntimeModuleState
{
    JSValue module = JS_UNDEFINED;
    JSValue namespace_object = JS_UNDEFINED;
    JSModuleDef* module_def = nullptr;
};

QuickJsRuntime::QuickJsRuntime()
{
    module_state_ = std::make_unique<RuntimeModuleState>();
    module_loader_state_ = std::make_unique<ModuleLoaderState>();

    runtime_ = JS_NewRuntime();
    if(runtime_ == nullptr)
    {
        throw std::runtime_error("Failed to create QuickJS runtime");
    }

    context_ = JS_NewContext(runtime_);
    if(context_ == nullptr)
    {
        Reset();
        throw std::runtime_error("Failed to create QuickJS context");
    }

    JS_SetModuleLoaderFunc(runtime_, NormalizeModuleName, signalbridge::LoadModule, module_loader_state_.get());
}

QuickJsRuntime::~QuickJsRuntime()
{
    Reset();
}

QuickJsRuntime::QuickJsRuntime(QuickJsRuntime&& other) noexcept
{
    *this = std::move(other);
}

QuickJsRuntime& QuickJsRuntime::operator=(QuickJsRuntime&& other) noexcept
{
    if(this != &other)
    {
        Reset();
        runtime_ = other.runtime_;
        context_ = other.context_;
        callback_state_ = std::move(other.callback_state_);
        module_state_ = std::move(other.module_state_);
        module_loader_state_ = std::move(other.module_loader_state_);
        other.runtime_ = nullptr;
        other.context_ = nullptr;
        if(context_ != nullptr)
        {
            JS_SetContextOpaque(context_, callback_state_.get());
            JS_SetModuleLoaderFunc(runtime_, NormalizeModuleName, signalbridge::LoadModule, module_loader_state_.get());
        }
    }
    return *this;
}

void QuickJsRuntime::SetCallbackState(std::unique_ptr<RuntimeCallbackState> state)
{
    callback_state_ = std::move(state);
    JS_SetContextOpaque(context_, callback_state_.get());
    RegisterRuntimeCallbacks(context_);
}

RuntimeCallbackState* QuickJsRuntime::MutableState()
{
    return callback_state_.get();
}

void QuickJsRuntime::Eval(const std::string& source, const std::string& name)
{
    JSValue result = JS_Eval(context_, source.c_str(), source.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(result))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, result);
        throw std::runtime_error(error);
    }
    JS_FreeValue(context_, result);
}

void QuickJsRuntime::LoadModule(const std::string& lookup_path, const std::vector<ScriptSource>& catalog)
{
    if(module_state_ == nullptr)
    {
        module_state_ = std::make_unique<RuntimeModuleState>();
    }
    if(module_loader_state_ == nullptr)
    {
        module_loader_state_ = std::make_unique<ModuleLoaderState>();
        JS_SetModuleLoaderFunc(runtime_, NormalizeModuleName, signalbridge::LoadModule, module_loader_state_.get());
    }

    ClearModuleState();
    module_loader_state_->SetCatalog(catalog);

    const ScriptSource* source = module_loader_state_->Find(lookup_path);
    if(source == nullptr)
    {
        throw std::runtime_error("entry module not found: " + lookup_path);
    }
    module_loader_state_->RecordLoaded(*source);

    const std::string module_name = NormalizeLookupPath(source->lookup_path);
    JSValue compiled = JS_Eval(
        context_,
        source->source.c_str(),
        source->source.size(),
        module_name.c_str(),
        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if(JS_IsException(compiled))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, compiled);
        throw std::runtime_error(error);
    }

    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    module_state_->module = compiled;
    module_state_->module_def = module;
}

void QuickJsRuntime::EvaluateModule()
{
    if(module_state_ == nullptr || JS_IsUndefined(module_state_->module))
    {
        return;
    }

    JSValue module = module_state_->module;
    JSModuleDef* module_def = module_state_->module_def;
    module_state_->module = JS_UNDEFINED;
    JSValue result = JS_EvalFunction(context_, module);
    if(JS_IsException(result))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, result);
        throw std::runtime_error(error);
    }
    JS_FreeValue(context_, result);

    JS_FreeValue(context_, module_state_->namespace_object);
    module_state_->namespace_object = JS_GetModuleNamespace(context_, module_def);
    if(JS_IsException(module_state_->namespace_object))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, module_state_->namespace_object);
        module_state_->namespace_object = JS_UNDEFINED;
        throw std::runtime_error(error);
    }
}

bool QuickJsRuntime::HasGlobal(const std::string& name) const
{
    JSValue global = JS_GetGlobalObject(context_);
    JSValue value = JS_GetPropertyStr(context_, global, name.c_str());
    const bool found = !JS_IsException(value) && !JS_IsUndefined(value);
    JS_FreeValue(context_, value);
    JS_FreeValue(context_, global);
    return found;
}

bool QuickJsRuntime::HasModuleExport(const std::string& name)
{
    if(module_state_ == nullptr || JS_IsUndefined(module_state_->namespace_object))
    {
        return false;
    }

    JSAtom atom = JS_NewAtom(context_, name.c_str());
    if(atom == JS_ATOM_NULL)
    {
        FormatException();
        return false;
    }
    const int has = JS_HasProperty(context_, module_state_->namespace_object, atom);
    JS_FreeAtom(context_, atom);
    if(has < 0)
    {
        FormatException();
        return false;
    }
    return has != 0;
}

QJsonValue QuickJsRuntime::CallGlobalJson(const std::string& name, const QJsonArray& args)
{
    JSValue global = JS_GetGlobalObject(context_);
    JSValue function = JS_GetPropertyStr(context_, global, name.c_str());
    if(JS_IsException(function))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, function);
        JS_FreeValue(context_, global);
        throw std::runtime_error(error);
    }
    if(JS_IsUndefined(function))
    {
        JS_FreeValue(context_, function);
        JS_FreeValue(context_, global);
        return QJsonValue();
    }

    std::vector<JSValue> js_args;
    js_args.reserve(static_cast<std::size_t>(args.size()));
    for(int idx = 0; idx < args.size(); idx++)
    {
        JSValue value = JsonToJsValue(context_, args.at(idx), "<arg>");
        if(JS_IsException(value))
        {
            const std::string error = FormatException();
            for(JSValue& existing : js_args)
            {
                JS_FreeValue(context_, existing);
            }
            JS_FreeValue(context_, value);
            JS_FreeValue(context_, function);
            JS_FreeValue(context_, global);
            throw std::runtime_error(error);
        }
        js_args.push_back(value);
    }

    JSValue result = JS_Call(context_, function, JS_UNDEFINED, static_cast<int>(js_args.size()), js_args.data());
    for(JSValue& value : js_args)
    {
        JS_FreeValue(context_, value);
    }
    JS_FreeValue(context_, function);
    JS_FreeValue(context_, global);

    if(JS_IsException(result))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, result);
        throw std::runtime_error(error);
    }

    const QJsonValue json = JsValueToJson(context_, result);
    JS_FreeValue(context_, result);
    return json;
}

QJsonValue QuickJsRuntime::CallModuleExportJson(const std::string& name, const QJsonArray& args)
{
    if(module_state_ == nullptr || JS_IsUndefined(module_state_->namespace_object))
    {
        return QJsonValue();
    }

    JSValue function = JS_GetPropertyStr(context_, module_state_->namespace_object, name.c_str());
    if(JS_IsException(function))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, function);
        throw std::runtime_error(error);
    }
    if(JS_IsUndefined(function))
    {
        JS_FreeValue(context_, function);
        return QJsonValue();
    }

    std::vector<JSValue> js_args;
    js_args.reserve(static_cast<std::size_t>(args.size()));
    for(int idx = 0; idx < args.size(); idx++)
    {
        JSValue value = JsonToJsValue(context_, args.at(idx), "<arg>");
        if(JS_IsException(value))
        {
            const std::string error = FormatException();
            for(JSValue& existing : js_args)
            {
                JS_FreeValue(context_, existing);
            }
            JS_FreeValue(context_, value);
            JS_FreeValue(context_, function);
            throw std::runtime_error(error);
        }
        js_args.push_back(value);
    }

    JSValue result = JS_Call(context_, function, JS_UNDEFINED, static_cast<int>(js_args.size()), js_args.data());
    for(JSValue& value : js_args)
    {
        JS_FreeValue(context_, value);
    }
    JS_FreeValue(context_, function);

    if(JS_IsException(result))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, result);
        throw std::runtime_error(error);
    }

    const QJsonValue json = JsValueToJson(context_, result);
    JS_FreeValue(context_, result);
    return json;
}

void QuickJsRuntime::SetGlobalJson(const std::string& name, const QJsonValue& value)
{
    JSValue global = JS_GetGlobalObject(context_);
    JSValue js_value = JsonToJsValue(context_, value, name);
    if(JS_IsException(js_value))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, js_value);
        JS_FreeValue(context_, global);
        throw std::runtime_error("failed to set global '" + name + "': " + error);
    }
    if(JS_SetPropertyStr(context_, global, name.c_str(), js_value) < 0)
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, global);
        throw std::runtime_error("failed to set global '" + name + "': " + error);
    }
    JS_FreeValue(context_, global);
}

void QuickJsRuntime::ApplyConfigurationValues(const QJsonObject& configuration)
{
    for(auto it = configuration.begin(); it != configuration.end(); ++it)
    {
        SetGlobalJson(it.key().toStdString(), it.value());
    }
}

void QuickJsRuntime::ApplyConfiguration(const ScriptMeta& meta, const QJsonObject& configuration)
{
    for(const QJsonObject& parameter : meta.control_parameters)
    {
        const QString property = parameter.value("property").toString();
        if(property.isEmpty())
        {
            continue;
        }

        const QJsonValue configured = configuration.value(property);
        SetGlobalJson(property.toStdString(), NormalizeParameterValue(parameter, configured));
    }
}

void QuickJsRuntime::ApplyConfigurationChange(const ScriptMeta& meta, const QJsonObject& configuration, const QString& property)
{
    if(property.isEmpty())
    {
        return;
    }

    bool applied = false;
    for(const QJsonObject& parameter : meta.control_parameters)
    {
        if(parameter.value("property").toString() != property)
        {
            continue;
        }

        const QJsonValue configured = configuration.value(property);
        SetGlobalJson(property.toStdString(), NormalizeParameterValue(parameter, configured));
        applied = true;
        break;
    }

    if(!applied && configuration.contains(property))
    {
        SetGlobalJson(property.toStdString(), configuration.value(property));
        applied = true;
    }

    if(!applied)
    {
        return;
    }

    const std::string callback = ConfigurationCallbackName(property);
    if(HasModuleExport(callback))
    {
        CallModuleExportJson(callback);
    }
}

void QuickJsRuntime::ApplyStaticMetadata(const ScriptMeta& meta)
{
    if(callback_state_ != nullptr)
    {
        RuntimeApplyStaticMetadata(*callback_state_, meta);
    }
}

QJsonObject QuickJsRuntime::TakeTopologyUpdate(bool force)
{
    return callback_state_ != nullptr ? RuntimeTakeTopologyUpdate(*callback_state_, force)
                                      : QJsonObject();
}

QJsonArray QuickJsRuntime::ExportProperties() const
{
    return callback_state_ != nullptr ? RuntimeExportProperties(*callback_state_)
                                      : QJsonArray();
}

std::vector<ScriptSource> QuickJsRuntime::LoadedModuleSources() const
{
    return module_loader_state_ != nullptr ? module_loader_state_->loaded_modules
                                           : std::vector<ScriptSource>();
}

void QuickJsRuntime::ClearModuleState()
{
    if(context_ == nullptr || module_state_ == nullptr)
    {
        return;
    }
    JS_FreeValue(context_, module_state_->module);
    JS_FreeValue(context_, module_state_->namespace_object);
    module_state_->module = JS_UNDEFINED;
    module_state_->namespace_object = JS_UNDEFINED;
    module_state_->module_def = nullptr;
}

void QuickJsRuntime::Reset()
{
    if(context_ != nullptr)
    {
        ClearModuleState();
        JS_SetContextOpaque(context_, nullptr);
        JS_FreeContext(context_);
        context_ = nullptr;
    }
    if(runtime_ != nullptr)
    {
        JS_FreeRuntime(runtime_);
        runtime_ = nullptr;
    }
    callback_state_.reset();
    module_state_.reset();
    module_loader_state_.reset();
}

std::string QuickJsRuntime::FormatException() const
{
    JSValue exception = JS_GetException(context_);
    const char* str = JS_ToCString(context_, exception);
    std::string message = str != nullptr ? str : "JavaScript error";
    if(str != nullptr)
    {
        JS_FreeCString(context_, str);
    }
    JS_FreeValue(context_, exception);
    return message;
}
}
