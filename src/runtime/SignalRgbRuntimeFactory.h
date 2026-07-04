#ifndef SIGNALBRIDGE_SIGNALRGB_RUNTIME_FACTORY_H
#define SIGNALBRIDGE_SIGNALRGB_RUNTIME_FACTORY_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QJsonObject>

#include "domain/ScriptTypes.h"
#include "hid/HidBackend.h"
#include "runtime/QuickJsRuntime.h"
#include "serial/SerialBackend.h"

namespace signalbridge
{
class SignalRgbRuntimeFactory
{
public:
    static QuickJsRuntime CreateScan(ScriptLogCallback log_callback = {}, std::string log_source = {});
    static QuickJsRuntime CreateValidation(const ScriptMeta& meta, ScriptLogCallback log_callback = {});
    static QuickJsRuntime CreateDeviceRuntime(
        std::shared_ptr<HidBackend> hid_backend,
        const ScriptMeta& meta,
        HidBackend::Handle primary_handle,
        const HidInfo& primary_hid,
        std::map<std::string, HidBackend::Handle> endpoint_handles,
        std::vector<EndpointDescriptor> endpoints,
        QJsonObject configuration = QJsonObject(),
        ScriptLogCallback log_callback = {},
        std::shared_ptr<SerialBackend> serial_backend = {},
        SerialInfo primary_serial = {});
};
}

#endif // SIGNALBRIDGE_SIGNALRGB_RUNTIME_FACTORY_H
