#include "runtime/SignalRgbRuntimeFactory.h"

#include <utility>

namespace signalbridge
{
QuickJsRuntime SignalRgbRuntimeFactory::CreateScan(ScriptLogCallback log_callback, std::string log_source)
{
    QuickJsRuntime runtime;
    auto state = std::make_unique<RuntimeCallbackState>();
    state->script_name = log_source.empty() ? std::string("scanner") : std::move(log_source);
    state->log_callback = std::move(log_callback);
    runtime.SetCallbackState(std::move(state));
    return runtime;
}

QuickJsRuntime SignalRgbRuntimeFactory::CreateValidation(const ScriptMeta& meta, ScriptLogCallback log_callback)
{
    QuickJsRuntime runtime = CreateScan(
        std::move(log_callback),
        meta.name.empty() ? meta.lookup_path : meta.name);
    runtime.LoadModule(meta.lookup_path, meta.module_sources);
    runtime.EvaluateModule();
    runtime.ApplyConfiguration(meta, QJsonObject());
    return runtime;
}

QuickJsRuntime SignalRgbRuntimeFactory::CreateDeviceRuntime(
    std::shared_ptr<HidBackend> hid_backend,
    const ScriptMeta& meta,
    HidBackend::Handle primary_handle,
    const HidInfo& primary_hid,
    std::map<std::string, HidBackend::Handle> endpoint_handles,
    std::vector<EndpointDescriptor> endpoints,
    QJsonObject configuration,
    ScriptLogCallback log_callback)
{
    QuickJsRuntime runtime;
    auto state = std::make_unique<RuntimeCallbackState>();
    state->hid_backend = std::move(hid_backend);
    state->active_handle = primary_handle;
    state->endpoint_handles = std::move(endpoint_handles);
    state->endpoints = std::move(endpoints);
    state->primary_hid = primary_hid;
    state->device.vid = primary_hid.vid;
    state->device.pid = primary_hid.pid;
    state->script_name = meta.name;
    state->log_callback = std::move(log_callback);
    runtime.SetCallbackState(std::move(state));
    runtime.ApplyStaticMetadata(meta);
    runtime.LoadModule(meta.lookup_path, meta.module_sources);
    runtime.EvaluateModule();
    runtime.ApplyConfigurationValues(configuration);
    runtime.ApplyConfiguration(meta, configuration);
    return runtime;
}
}
