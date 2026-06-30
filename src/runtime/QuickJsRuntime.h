#ifndef SIGNALBRIDGE_QUICKJS_RUNTIME_H
#define SIGNALBRIDGE_QUICKJS_RUNTIME_H

#include <memory>
#include <string>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "domain/ScriptTypes.h"
#include "runtime/RuntimeBindings.h"

struct JSContext;
struct JSRuntime;

namespace signalbridge
{
struct ModuleLoaderState;
struct RuntimeModuleState;

class QuickJsRuntime
{
public:
    QuickJsRuntime();
    ~QuickJsRuntime();

    QuickJsRuntime(const QuickJsRuntime&) = delete;
    QuickJsRuntime& operator=(const QuickJsRuntime&) = delete;

    QuickJsRuntime(QuickJsRuntime&& other) noexcept;
    QuickJsRuntime& operator=(QuickJsRuntime&& other) noexcept;

    void SetCallbackState(std::unique_ptr<RuntimeCallbackState> state);
    RuntimeCallbackState* CallbackState();

    void Eval(const std::string& source, const std::string& name);
    void LoadModule(const std::string& lookup_path, const std::vector<ScriptSource>& catalog);
    void EvaluateModule();
    bool HasGlobal(const std::string& name) const;
    bool HasModuleExport(const std::string& name);
    QJsonValue CallGlobalJson(const std::string& name, const QJsonArray& args = QJsonArray());
    QJsonValue CallModuleExportJson(const std::string& name, const QJsonArray& args = QJsonArray());
    void SetGlobalJson(const std::string& name, const QJsonValue& value);
    void ApplyConfigurationValues(const QJsonObject& configuration);
    void ApplyConfiguration(const ScriptMeta& meta, const QJsonObject& configuration);
    void ApplyStaticMetadata(const ScriptMeta& meta);
    std::vector<ScriptSource> LoadedModuleSources() const;

private:
    void Reset();
    void ClearModuleState();
    std::string FormatException() const;

    JSRuntime* runtime_ = nullptr;
    JSContext* context_ = nullptr;
    std::unique_ptr<RuntimeCallbackState> callback_state_;
    std::unique_ptr<RuntimeModuleState> module_state_;
    std::unique_ptr<ModuleLoaderState> module_loader_state_;
};
}

#endif // SIGNALBRIDGE_QUICKJS_RUNTIME_H
