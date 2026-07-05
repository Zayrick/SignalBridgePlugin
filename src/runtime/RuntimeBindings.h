#ifndef SIGNALBRIDGE_RUNTIME_BINDINGS_H
#define SIGNALBRIDGE_RUNTIME_BINDINGS_H

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "domain/ColorFrame.h"
#include "domain/ScriptTypes.h"
#include "hid/HidBackend.h"
#include "serial/SerialBackend.h"

struct JSContext;
struct JSModuleDef;

namespace signalbridge
{
struct RuntimeChannelState
{
    std::string name;
    int led_count = 0;
    int led_limit = 0;
    std::vector<std::uint8_t> colors;
    bool needs_pulse = true;
};

struct RuntimeSubdeviceState
{
    std::string name;
    std::string display_name;
    std::string image_url;
    int width = 1;
    int height = 1;
    std::vector<std::string> led_names;
    std::vector<std::pair<int, int>> led_positions;
    std::map<std::pair<int, int>, std::size_t> led_position_index;
    std::vector<std::uint8_t> colors;
};

struct RuntimeDeviceState
{
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    int width = 1;
    int height = 1;
    int led_count = 0;
    int total_led_limit = 0;
    std::vector<std::string> led_names;
    std::vector<std::pair<int, int>> led_positions;
    std::string name;
    std::string image_url;
    RuntimeColorFrame main_frame;
    std::vector<RuntimeChannelState> channels;
    std::vector<RuntimeSubdeviceState> subdevices;
    bool topology_dirty = true;
    int pulse_phase = 0;
};

struct RuntimeCallbackState
{
    std::shared_ptr<HidBackend> hid_backend;
    HidBackend::Handle active_handle = 0;
    std::map<std::string, HidBackend::Handle> endpoint_handles;
    std::vector<EndpointDescriptor> endpoints;
    HidInfo primary_hid;
    std::size_t last_read_size = 0;
    std::shared_ptr<SerialBackend> serial_backend;
    SerialInfo primary_serial;
    SerialInfo active_serial;
    std::unique_ptr<SerialConnection> serial_connection;
    std::string script_name;
    ScriptLogCallback log_callback;
    RuntimeDeviceState device;
    std::vector<QJsonObject> properties;
    std::map<std::string, QJsonValue> global_context;
};

void RegisterRuntimeCallbacks(JSContext* context);
RuntimeCallbackState* RuntimeCallbacks(JSContext* context);
bool IsSignalBridgeHostModule(const std::string& specifier);
JSModuleDef* LoadSignalBridgeHostModule(JSContext* context, const char* module_name);
void RuntimeApplyStaticMetadata(RuntimeCallbackState& state, const ScriptMeta& meta);
QJsonObject RuntimeTakeTopologyUpdate(RuntimeCallbackState& state, bool force);
QJsonArray RuntimeExportProperties(const RuntimeCallbackState& state);
}

#endif // SIGNALBRIDGE_RUNTIME_BINDINGS_H
