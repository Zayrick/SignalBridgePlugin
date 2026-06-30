#ifndef SIGNALBRIDGE_RUNTIME_BINDINGS_H
#define SIGNALBRIDGE_RUNTIME_BINDINGS_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "domain/ScriptTypes.h"
#include "hid/HidBackend.h"

struct JSContext;

namespace signalbridge
{
struct RuntimeCallbackState
{
    std::shared_ptr<HidBackend> hid_backend;
    HidBackend::Handle active_handle = 0;
    std::map<std::string, HidBackend::Handle> endpoint_handles;
    std::vector<EndpointDescriptor> endpoints;
    std::size_t last_read_size = 0;
    std::string script_name;
    ScriptLogCallback log_callback;
};

void RegisterRuntimeCallbacks(JSContext* context);
RuntimeCallbackState* RuntimeCallbacks(JSContext* context);
}

#endif // SIGNALBRIDGE_RUNTIME_BINDINGS_H
