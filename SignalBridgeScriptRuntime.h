#ifndef SIGNALBRIDGESCRIPTRUNTIME_H
#define SIGNALBRIDGESCRIPTRUNTIME_H

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "SignalBridgeHid.h"

struct JSContext;
struct JSRuntime;

struct SignalBridgeScriptSource
{
    std::string source_path;
    std::string lookup_path;
    std::string source;
};

struct SignalBridgeScriptMeta
{
    std::string source_path;
    std::string lookup_path;
    std::string name;
    std::optional<std::uint16_t> vid;
    std::vector<std::uint16_t> pids;
    unsigned int width = 1;
    unsigned int height = 1;
    std::string device_type;
    std::string publisher;
    std::string image_url;
    std::vector<std::string> led_names;
    std::vector<std::pair<int, int>> led_positions;
    std::vector<QJsonObject> control_parameters;
    bool has_validate = false;
    std::vector<SignalBridgeScriptSource> module_sources;
};

struct SignalBridgeEndpointDescriptor
{
    int interface_number = 0;
    int usage = 0;
    int usage_page = 0;
};

struct SignalBridgeJsCallbackState;
struct SignalBridgeJsModuleState;
struct SignalBridgeModuleLoaderState;

class SignalBridgeJsRuntime
{
public:
    SignalBridgeJsRuntime();
    ~SignalBridgeJsRuntime();

    SignalBridgeJsRuntime(const SignalBridgeJsRuntime&) = delete;
    SignalBridgeJsRuntime& operator=(const SignalBridgeJsRuntime&) = delete;

    SignalBridgeJsRuntime(SignalBridgeJsRuntime&& other) noexcept;
    SignalBridgeJsRuntime& operator=(SignalBridgeJsRuntime&& other) noexcept;

    static SignalBridgeJsRuntime CreateScan();
    static SignalBridgeJsRuntime CreateValidation(const SignalBridgeScriptMeta& meta);
    static SignalBridgeJsRuntime CreateRuntime(
        std::shared_ptr<SignalBridgeHidBackend> hid_backend,
        const SignalBridgeScriptMeta& meta,
        SignalBridgeHidBackend::Handle primary_handle,
        const SignalBridgeHidInfo& primary_hid,
        std::map<std::string, SignalBridgeHidBackend::Handle> endpoint_handles,
        std::vector<SignalBridgeEndpointDescriptor> endpoints,
        QJsonObject configuration = QJsonObject());

    void Eval(const std::string& source, const std::string& name);
    void LoadModule(
        const std::string& lookup_path,
        const std::vector<SignalBridgeScriptSource>& catalog);
    void EvaluateModule();
    bool HasGlobal(const std::string& name) const;
    bool HasModuleExport(const std::string& name);
    QJsonValue CallGlobalJson(const std::string& name, const QJsonArray& args = QJsonArray());
    QJsonValue CallModuleExportJson(const std::string& name, const QJsonArray& args = QJsonArray());
    void SetGlobalJson(const std::string& name, const QJsonValue& value);
    void ApplyConfigurationValues(const QJsonObject& configuration);
    void ApplyConfiguration(const SignalBridgeScriptMeta& meta, const QJsonObject& configuration);
    std::vector<SignalBridgeScriptSource> LoadedModuleSources() const;

private:
    explicit SignalBridgeJsRuntime(bool create_context);

    void Reset();
    void ClearModuleState();
    void RegisterCallbacks();
    void ApplyStaticMetadata(const SignalBridgeScriptMeta& meta);

    std::string FormatException() const;

    JSRuntime* runtime_ = nullptr;
    JSContext* context_ = nullptr;
    std::unique_ptr<SignalBridgeJsCallbackState> callback_state_;
    std::unique_ptr<SignalBridgeJsModuleState> module_state_;
    std::unique_ptr<SignalBridgeModuleLoaderState> module_loader_state_;
};

#endif // SIGNALBRIDGESCRIPTRUNTIME_H
