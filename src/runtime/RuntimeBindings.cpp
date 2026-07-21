#include "runtime/RuntimeBindings.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <thread>

#include <QJsonArray>
#include <QJsonObject>

#include "domain/ControlParameters.h"
#include "runtime/RuntimeBindingUtils.h"
#include "runtime/QuickJsValue.h"
#include "runtime/signalrgb/SignalRgbEnvironmentGlobals.h"
#include "runtime/signalrgb/SignalRgbModuleRegistry.h"
#include "runtime/signalrgb/SystemInfoModule.h"

namespace signalbridge
{
RuntimeCallbackState* RuntimeCallbacks(JSContext* context)
{
    return static_cast<RuntimeCallbackState*>(JS_GetContextOpaque(context));
}

namespace
{
constexpr const char* kHostModuleName = "signalbridge:host";

std::string FindEndpointHandleKey(int interface_number, int usage, int usage_page)
{
    std::ostringstream exact;
    exact << interface_number << ":" << usage << ":" << usage_page;
    return exact.str();
}

bool UsagePagesCompatible(int a, int b)
{
    return a == b || (a >= 0xFF00 && b >= 0xFF00);
}

std::string FormatJsValue(JSContext* context, JSValueConst value)
{
    if(JS_IsString(value))
    {
        return JsToString(context, value);
    }

    JSValue json = JS_JSONStringify(context, value, JS_UNDEFINED, JS_UNDEFINED);
    if(!JS_IsException(json) && !JS_IsUndefined(json))
    {
        const char* text = JS_ToCString(context, json);
        std::string result = text != nullptr ? text : std::string();
        if(text != nullptr)
        {
            JS_FreeCString(context, text);
        }
        JS_FreeValue(context, json);
        return result;
    }
    JS_FreeValue(context, json);
    return JsToString(context, value);
}

std::string FormatArguments(JSContext* context, int argc, JSValueConst* argv)
{
    std::string message;
    for(int idx = 0; idx < argc; idx++)
    {
        if(idx > 0)
        {
            message += " ";
        }
        message += FormatJsValue(context, argv[idx]);
    }
    return message;
}

void EmitLog(JSContext* context, const std::string& message)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    const std::string source = state != nullptr && !state->script_name.empty()
                                   ? state->script_name
                                   : std::string("script");
    std::fprintf(stderr, "[SignalBridge][%s] %s\n", source.c_str(), message.c_str());
    if(state != nullptr && state->log_callback)
    {
        state->log_callback(source, message);
    }
}

std::vector<std::uint8_t> PreparePacket(JSContext* context, JSValueConst value, int length)
{
    std::vector<std::uint8_t> bytes = JsValueToBytes(context, value);
    if(length >= 0)
    {
        bytes.resize(static_cast<std::size_t>(length), 0);
    }
    return bytes;
}

std::vector<std::uint8_t> MergeReadResult(
    const std::vector<std::uint8_t>& seed,
    const std::vector<std::uint8_t>& raw,
    std::size_t length)
{
    std::vector<std::uint8_t> packet(length, 0);
    for(std::size_t idx = 0; idx < packet.size() && idx < seed.size(); idx++)
    {
        packet[idx] = seed[idx];
    }
    for(std::size_t idx = 0; idx < packet.size() && idx < raw.size(); idx++)
    {
        packet[idx] = raw[idx];
    }
    return packet;
}

HidBackend::Handle FindEndpointHandle(
    const std::map<std::string, HidBackend::Handle>& handles,
    int interface_number,
    int usage,
    int usage_page)
{
    const std::string exact = FindEndpointHandleKey(interface_number, usage, usage_page);
    const auto exact_it = handles.find(exact);
    if(exact_it != handles.end())
    {
        return exact_it->second;
    }

    std::vector<HidBackend::Handle> matched;
    for(const auto& item : handles)
    {
        int iface = 0;
        int candidate_usage = 0;
        int candidate_page = 0;
        char colon1 = 0;
        char colon2 = 0;
        std::istringstream stream(item.first);
        if(stream >> iface >> colon1 >> candidate_usage >> colon2 >> candidate_page &&
           colon1 == ':' && colon2 == ':' &&
           iface == interface_number &&
           candidate_usage == usage &&
           UsagePagesCompatible(candidate_page, usage_page))
        {
            matched.push_back(item.second);
        }
    }
    if(matched.size() == 1)
    {
        return matched.front();
    }

    matched.clear();
    for(const auto& item : handles)
    {
        int iface = 0;
        int candidate_usage = 0;
        int candidate_page = 0;
        char colon1 = 0;
        char colon2 = 0;
        std::istringstream stream(item.first);
        if(stream >> iface >> colon1 >> candidate_usage >> colon2 >> candidate_page &&
           colon1 == ':' && colon2 == ':' &&
           candidate_usage == usage &&
           UsagePagesCompatible(candidate_page, usage_page))
        {
            matched.push_back(item.second);
        }
    }
    return matched.size() == 1 ? matched.front() : 0;
}

QJsonArray EndpointsToJson(const std::vector<EndpointDescriptor>& endpoints)
{
    QJsonArray array;
    for(const EndpointDescriptor& endpoint : endpoints)
    {
        QJsonObject object;
        object.insert("interface", endpoint.interface_number);
        object.insert("usage", endpoint.usage);
        object.insert("usage_page", endpoint.usage_page);
        object.insert("collection", 0);
        array.append(object);
    }
    return array;
}

QJsonArray StringArrayToJson(const std::vector<std::string>& values)
{
    QJsonArray array;
    for(const std::string& value : values)
    {
        array.append(QString::fromStdString(value));
    }
    return array;
}

QJsonArray PositionArrayToJson(const std::vector<std::pair<int, int>>& values)
{
    QJsonArray array;
    for(const auto& position : values)
    {
        QJsonArray item;
        item.append(position.first);
        item.append(position.second);
        array.append(item);
    }
    return array;
}

std::vector<std::string> JsStringArray(JSContext* context, JSValueConst value)
{
    const QJsonArray array = JsValueToJson(context, value).toArray();
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(array.size()));
    for(const QJsonValue& item : array)
    {
        result.push_back(item.toString().toStdString());
    }
    return result;
}

std::vector<std::pair<int, int>> JsPositionArray(JSContext* context, JSValueConst value)
{
    const QJsonArray array = JsValueToJson(context, value).toArray();
    std::vector<std::pair<int, int>> result;
    for(const QJsonValue& item : array)
    {
        if(item.isArray())
        {
            const QJsonArray position = item.toArray();
            if(position.size() >= 2)
            {
                result.push_back({ position.at(0).toInt(), position.at(1).toInt() });
            }
        }
        else if(item.isObject())
        {
            const QJsonObject position = item.toObject();
            result.push_back({ position.value("x").toInt(), position.value("y").toInt() });
        }
    }
    return result;
}

int CurrentMainLedCount(const RuntimeDeviceState& device)
{
    if(!device.led_positions.empty())
    {
        return static_cast<int>(device.led_positions.size());
    }
    return std::max(0, device.led_count);
}

int MainCanvasLedCount(const RuntimeDeviceState& device)
{
    return std::max(1, device.width) * std::max(1, device.height);
}

int SubdeviceLedCount(const RuntimeSubdeviceState& subdevice)
{
    return static_cast<int>(std::max(subdevice.led_names.size(), subdevice.led_positions.size()));
}

RuntimeChannelState* FindChannel(RuntimeDeviceState& device, const std::string& name)
{
    for(RuntimeChannelState& channel : device.channels)
    {
        if(channel.name == name)
        {
            return &channel;
        }
    }
    return nullptr;
}

const RuntimeChannelState* FindChannel(const RuntimeDeviceState& device, const std::string& name)
{
    for(const RuntimeChannelState& channel : device.channels)
    {
        if(channel.name == name)
        {
            return &channel;
        }
    }
    return nullptr;
}

RuntimeSubdeviceState* FindSubdevice(RuntimeDeviceState& device, const std::string& name)
{
    for(RuntimeSubdeviceState& subdevice : device.subdevices)
    {
        if(subdevice.name == name)
        {
            return &subdevice;
        }
    }
    return nullptr;
}

const RuntimeSubdeviceState* FindSubdevice(const RuntimeDeviceState& device, const std::string& name)
{
    for(const RuntimeSubdeviceState& subdevice : device.subdevices)
    {
        if(subdevice.name == name)
        {
            return &subdevice;
        }
    }
    return nullptr;
}

void RebuildSubdevicePositionIndex(RuntimeSubdeviceState& subdevice)
{
    subdevice.led_position_index.clear();
    for(std::size_t idx = 0; idx < subdevice.led_positions.size(); idx++)
    {
        subdevice.led_position_index[subdevice.led_positions[idx]] = idx;
    }
}

void ResizeFrameColors(std::vector<std::uint8_t>& colors, int led_count)
{
    if(led_count <= 0)
    {
        colors.clear();
        return;
    }
    colors.resize(static_cast<std::size_t>(led_count) * 3, 0);
}

std::string ThisChannelName(JSContext* context, JSValueConst this_val)
{
    JSValue name = JS_GetPropertyStr(context, this_val, "__name");
    const std::string result = JsToString(context, name);
    JS_FreeValue(context, name);
    return result;
}

void ColorOrderMap(const std::string& order, int map[3])
{
    std::string normalized = order;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if(normalized == "RBG")
    {
        map[1] = 2;
        map[2] = 1;
    }
    else if(normalized == "GRB")
    {
        map[0] = 1;
        map[1] = 0;
    }
    else if(normalized == "GBR")
    {
        map[0] = 1;
        map[1] = 2;
        map[2] = 0;
    }
    else if(normalized == "BRG")
    {
        map[0] = 2;
        map[1] = 0;
        map[2] = 1;
    }
    else if(normalized == "BGR")
    {
        map[0] = 2;
        map[2] = 0;
    }
}

std::uint8_t ColorByte(const std::vector<std::uint8_t>& colors, std::size_t index)
{
    return index < colors.size() ? colors[index] : static_cast<std::uint8_t>(0);
}

JSValue RgbToJsArray(JSContext* context, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    JSValue array = JS_NewArray(context);
    JS_SetPropertyUint32(context, array, 0, JS_NewInt32(context, r));
    JS_SetPropertyUint32(context, array, 1, JS_NewInt32(context, g));
    JS_SetPropertyUint32(context, array, 2, JS_NewInt32(context, b));
    return array;
}

JSValue SeparateColorsToJs(JSContext* context, const std::vector<std::uint8_t>& colors, const int map[3])
{
    JSValue channels = JS_NewArray(context);
    JSValue first = JS_NewArray(context);
    JSValue second = JS_NewArray(context);
    JSValue third = JS_NewArray(context);
    const std::size_t led_count = (colors.size() + 2) / 3;
    for(std::size_t led_idx = 0; led_idx < led_count; led_idx++)
    {
        const std::size_t base = led_idx * 3;
        JS_SetPropertyUint32(context, first, static_cast<std::uint32_t>(led_idx), JS_NewInt32(context, ColorByte(colors, base + map[0])));
        JS_SetPropertyUint32(context, second, static_cast<std::uint32_t>(led_idx), JS_NewInt32(context, ColorByte(colors, base + map[1])));
        JS_SetPropertyUint32(context, third, static_cast<std::uint32_t>(led_idx), JS_NewInt32(context, ColorByte(colors, base + map[2])));
    }
    JS_SetPropertyUint32(context, channels, 0, first);
    JS_SetPropertyUint32(context, channels, 1, second);
    JS_SetPropertyUint32(context, channels, 2, third);
    return channels;
}

JSValue InlineColorsToJs(JSContext* context, const std::vector<std::uint8_t>& colors, const int map[3])
{
    if(map[0] == 0 && map[1] == 1 && map[2] == 2)
    {
        return BytesToJsArray(context, colors);
    }

    JSValue array = JS_NewArray(context);
    std::uint32_t out_idx = 0;
    for(std::size_t idx = 0; idx < colors.size(); idx += 3)
    {
        JS_SetPropertyUint32(context, array, out_idx++, JS_NewInt32(context, ColorByte(colors, idx + map[0])));
        JS_SetPropertyUint32(context, array, out_idx++, JS_NewInt32(context, ColorByte(colors, idx + map[1])));
        JS_SetPropertyUint32(context, array, out_idx++, JS_NewInt32(context, ColorByte(colors, idx + map[2])));
    }
    return array;
}

JSValue ColorsToJs(JSContext* context, const std::vector<std::uint8_t>& colors, const std::string& format, const std::string& order)
{
    int map[3] = { 0, 1, 2 };
    ColorOrderMap(order, map);
    if(format == "Separate" || format == "Seperate")
    {
        return SeparateColorsToJs(context, colors, map);
    }
    return InlineColorsToJs(context, colors, map);
}

JSValue HidWriteJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->hid_backend || state->active_handle == 0 || argc < 1)
    {
        return JS_NewInt32(context, 0);
    }
    try
    {
        const int length = argc > 1 ? JsToInt(context, argv[1], -1) : -1;
        const std::vector<std::uint8_t> bytes = PreparePacket(context, argv[0], length);
        return JS_NewInt32(context, static_cast<int>(state->hid_backend->Write(state->active_handle, bytes)));
    }
    catch(...)
    {
        return JS_NewInt32(context, 0);
    }
}

JSValue HidReadJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->hid_backend || state->active_handle == 0)
    {
        return JS_NewArray(context);
    }
    try
    {
        std::vector<std::uint8_t> seed;
        std::size_t length = 64;
        int timeout_ms = 1000;
        if(argc > 0 && JS_IsNumber(argv[0]))
        {
            length = JsToSize(context, argv[0], 64);
            timeout_ms = argc > 1 ? JsToInt(context, argv[1], 1000) : 1000;
        }
        else
        {
            seed = argc > 0 ? JsValueToBytes(context, argv[0]) : std::vector<std::uint8_t>();
            length = argc > 1 ? JsToSize(context, argv[1], 64) : 64;
            timeout_ms = argc > 2 ? JsToInt(context, argv[2], 1000) : 1000;
        }

        std::vector<std::uint8_t> bytes = state->hid_backend->Read(state->active_handle, length, timeout_ms);
        state->last_read_size = bytes.size();
        return BytesToJsArray(context, seed.empty() ? bytes : MergeReadResult(seed, bytes, length));
    }
    catch(...)
    {
        state->last_read_size = 0;
        return JS_NewArray(context);
    }
}

JSValue HidSendReportJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->hid_backend || state->active_handle == 0 || argc < 1)
    {
        return JS_NewInt32(context, 0);
    }
    try
    {
        const int length = argc > 1 ? JsToInt(context, argv[1], -1) : -1;
        const std::vector<std::uint8_t> bytes = PreparePacket(context, argv[0], length);
        return JS_NewInt32(context, static_cast<int>(state->hid_backend->SendFeatureReport(state->active_handle, bytes)));
    }
    catch(...)
    {
        return JS_NewInt32(context, 0);
    }
}

JSValue HidGetReportJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->hid_backend || state->active_handle == 0)
    {
        return JS_NewArray(context);
    }
    try
    {
        std::vector<std::uint8_t> seed = argc > 0 ? JsValueToBytes(context, argv[0]) : std::vector<std::uint8_t>();
        const int report_id = !seed.empty() ? seed[0] : 0;
        const std::size_t length = argc > 1 ? JsToSize(context, argv[1], 64) : 64;
        std::vector<std::uint8_t> bytes = state->hid_backend->GetFeatureReport(
            state->active_handle,
            static_cast<std::uint8_t>(std::clamp(report_id, 0, 255)),
            length);
        state->last_read_size = bytes.size();
        return BytesToJsArray(context, MergeReadResult(seed, bytes, length));
    }
    catch(...)
    {
        state->last_read_size = 0;
        return JS_NewArray(context);
    }
}

JSValue HidInputReportJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->hid_backend || state->active_handle == 0)
    {
        return JS_NewArray(context);
    }
    try
    {
        const std::size_t length = argc > 0 ? JsToSize(context, argv[0], 64) : 64;
        const int timeout_ms = argc > 1 ? JsToInt(context, argv[1], 1000) : 1000;
        std::vector<std::uint8_t> bytes = state->hid_backend->Read(state->active_handle, length, timeout_ms);
        state->last_read_size = bytes.size();
        return BytesToJsArray(context, MergeReadResult({}, bytes, length));
    }
    catch(...)
    {
        state->last_read_size = 0;
        return JS_NewArray(context);
    }
}

JSValue HidSetEndpointJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || argc < 1)
    {
        return JS_NewBool(context, false);
    }

    int interface_number = -1;
    int usage = -1;
    int usage_page = -1;
    if(JS_IsObject(argv[0]) && argc == 1)
    {
        JSValue value = JS_GetPropertyStr(context, argv[0], "interface");
        interface_number = JsToInt(context, value, -1);
        JS_FreeValue(context, value);
        value = JS_GetPropertyStr(context, argv[0], "usage");
        usage = JsToInt(context, value, -1);
        JS_FreeValue(context, value);
        value = JS_GetPropertyStr(context, argv[0], "usage_page");
        usage_page = JsToInt(context, value, -1);
        JS_FreeValue(context, value);
    }
    else
    {
        interface_number = JsToInt(context, argv[0], -1);
        usage = argc > 1 ? JsToInt(context, argv[1], -1) : -1;
        usage_page = argc > 2 ? JsToInt(context, argv[2], -1) : -1;
    }

    if(usage < 0 || usage_page < 0)
    {
        return JS_NewBool(context, false);
    }

    const HidBackend::Handle handle = FindEndpointHandle(
        state->endpoint_handles,
        interface_number,
        usage,
        usage_page);
    if(handle == 0)
    {
        return JS_NewBool(context, false);
    }
    state->active_handle = handle;
    return JS_NewBool(context, true);
}

JSValue HidGetEndpointsJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    return JsonToJsValue(context, state != nullptr ? EndpointsToJson(state->endpoints) : QJsonArray(), "<endpoints>");
}

JSValue HidFlushJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->hid_backend || state->active_handle == 0)
    {
        return JS_NewInt32(context, 0);
    }
    try
    {
        const std::size_t flushed = state->hid_backend->Flush(state->active_handle);
        state->last_read_size = 0;
        return JS_NewInt32(context, static_cast<int>(flushed));
    }
    catch(...)
    {
        return JS_NewInt32(context, 0);
    }
}

JSValue HidLastReadSizeJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    return JS_NewInt32(context, static_cast<int>(state != nullptr ? state->last_read_size : 0));
}

JSValue LogJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    EmitLog(context, FormatArguments(context, argc, argv));
    return JS_UNDEFINED;
}

JSValue PauseJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    const int ms = std::max(0, argc > 0 ? JsToInt(context, argv[0], 0) : 0);
    if(ms > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    return JS_UNDEFINED;
}

JSValue ConsoleWarnJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    EmitLog(context, "[WARN] " + FormatArguments(context, argc, argv));
    return JS_UNDEFINED;
}

JSValue ConsoleErrorJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    EmitLog(context, "[ERROR] " + FormatArguments(context, argc, argv));
    return JS_UNDEFINED;
}

JSValue DeviceGetHidInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    QJsonObject info;
    if(state != nullptr)
    {
        info.insert("vid", static_cast<int>(state->device.vid));
        info.insert("pid", static_cast<int>(state->device.pid));
    }
    return JsonToJsValue(context, info, "<hid-info>");
}

JSValue DeviceGetDeviceInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    QJsonObject info;
    if(state != nullptr)
    {
        info.insert("vid", static_cast<int>(state->device.vid));
        info.insert("pid", static_cast<int>(state->device.pid));
        info.insert("name", QString::fromStdString(state->device.name));
        info.insert("product", QString::fromStdString(state->primary_hid.product));
    }
    return JsonToJsValue(context, info, "<device-info>");
}

JSValue DeviceColorJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || argc < 2)
    {
        return RgbToJsArray(context, 0, 0, 0);
    }

    const int x = JsToInt(context, argv[0], 0);
    const int y = JsToInt(context, argv[1], 0);
    const RuntimeColorFrame& frame = state->device.main_frame;
    const int width = std::max(1, frame.width);
    const int idx = ((y * width) + x) * 3;
    if(idx >= 0 && static_cast<std::size_t>(idx + 2) < frame.colors.size())
    {
        return RgbToJsArray(
            context,
            frame.colors[static_cast<std::size_t>(idx)],
            frame.colors[static_cast<std::size_t>(idx + 1)],
            frame.colors[static_cast<std::size_t>(idx + 2)]);
    }
    return RgbToJsArray(context, 0, 0, 0);
}

JSValue DeviceSubdeviceColorJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || argc < 3)
    {
        return RgbToJsArray(context, 0, 0, 0);
    }

    const std::string name = JsToString(context, argv[0]);
    const int x = JsToInt(context, argv[1], 0);
    const int y = JsToInt(context, argv[2], 0);
    const RuntimeSubdeviceState* subdevice = FindSubdevice(state->device, name);
    if(subdevice == nullptr)
    {
        return RgbToJsArray(context, 0, 0, 0);
    }
    const auto it = subdevice->led_position_index.find({ x, y });
    if(it != subdevice->led_position_index.end())
    {
        const std::size_t color_idx = it->second * 3;
        if(color_idx + 2 < subdevice->colors.size())
        {
            return RgbToJsArray(context, subdevice->colors[color_idx], subdevice->colors[color_idx + 1], subdevice->colors[color_idx + 2]);
        }
    }
    return RgbToJsArray(context, 0, 0, 0);
}

JSValue DeviceGetLedCountJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr)
    {
        return JS_NewInt32(context, 0);
    }
    if(!state->device.channels.empty())
    {
        int total = 0;
        for(const RuntimeChannelState& channel : state->device.channels)
        {
            total += std::max(0, channel.led_count);
        }
        return JS_NewInt32(context, total);
    }
    const int main_count = CurrentMainLedCount(state->device);
    return JS_NewInt32(context, main_count > 0 ? main_count : MainCanvasLedCount(state->device));
}

JSValue DeviceProductIdJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    return JS_NewInt32(context, state != nullptr ? state->device.pid : 0);
}

JSValue DeviceSetLedLimitJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc > 0)
    {
        const int limit = std::max(0, JsToInt(context, argv[0], 0));
        if(state->device.total_led_limit != limit)
        {
            state->device.total_led_limit = limit;
            state->device.topology_dirty = true;
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceSetNameJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr)
    {
        const std::string name = argc > 0 && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0])
                                     ? JsToString(context, argv[0])
                                     : std::string();
        if(state->device.name != name)
        {
            state->device.name = name;
            state->device.topology_dirty = true;
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceSetImageFromUrlJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr)
    {
        const std::string image = argc > 0 && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0])
                                      ? JsToString(context, argv[0])
                                      : std::string();
        if(state->device.image_url != image)
        {
            state->device.image_url = image;
            state->device.topology_dirty = true;
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceSetSizeJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc > 0)
    {
        const QJsonArray size = JsValueToJson(context, argv[0]).toArray();
        if(size.size() >= 2)
        {
            const int width = std::max(1, size.at(0).toInt(1));
            const int height = std::max(1, size.at(1).toInt(1));
            if(state->device.width != width || state->device.height != height)
            {
                state->device.width = width;
                state->device.height = height;
                state->device.topology_dirty = true;
            }
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceSetControllableLedsJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr)
    {
        const std::vector<std::string> names = argc > 0 ? JsStringArray(context, argv[0]) : std::vector<std::string>();
        const std::vector<std::pair<int, int>> positions = argc > 1 ? JsPositionArray(context, argv[1]) : std::vector<std::pair<int, int>>();
        const int led_count = static_cast<int>(!positions.empty() ? positions.size() : names.size());
        if(state->device.led_names != names || state->device.led_positions != positions || state->device.led_count != led_count)
        {
            state->device.led_names = names;
            state->device.led_positions = positions;
            state->device.led_count = led_count;
            state->device.topology_dirty = true;
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceAddPropertyJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || argc < 1)
    {
        return JS_UNDEFINED;
    }
    QJsonObject property = JsValueToJson(context, argv[0]).toObject();
    const QString name = property.value("property").toString().trimmed();
    if(name.isEmpty())
    {
        return JS_UNDEFINED;
    }
    property.insert("property", name);
    if(!property.contains("label"))
    {
        property.insert("label", name);
    }
    if(!property.contains("type"))
    {
        property.insert("type", "text");
    }

    auto existing = std::find_if(state->properties.begin(), state->properties.end(), [&](const QJsonObject& item) {
        return item.value("property").toString() == name;
    });
    if(existing != state->properties.end())
    {
        *existing = property;
    }
    else
    {
        state->properties.push_back(property);
    }

    JSValue global = JS_GetGlobalObject(context);
    JSValue existing_value = JS_GetPropertyStr(context, global, name.toUtf8().constData());
    const QJsonValue normalized = NormalizeParameterValue(property, JsValueToJson(context, existing_value));
    JS_FreeValue(context, existing_value);
    JS_SetPropertyStr(context, global, name.toUtf8().constData(), JsonToJsValue(context, normalized, name.toStdString()));
    JS_FreeValue(context, global);
    return JS_UNDEFINED;
}

JSValue DeviceGetPropertyJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || argc < 1)
    {
        return JS_UNDEFINED;
    }
    const QString name = QString::fromStdString(JsToString(context, argv[0]));
    for(const QJsonObject& property : state->properties)
    {
        if(property.value("property").toString() == name)
        {
            return JsonToJsValue(context, property, "<property>");
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceRemovePropertyJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc > 0)
    {
        const QString name = QString::fromStdString(JsToString(context, argv[0]));
        state->properties.erase(std::remove_if(state->properties.begin(), state->properties.end(), [&](const QJsonObject& item) {
            return item.value("property").toString() == name;
        }), state->properties.end());
    }
    return JS_UNDEFINED;
}

JSValue DeviceGetComponentNamesJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    QJsonArray names;
    if(state != nullptr && !state->device.channels.empty())
    {
        for(const RuntimeChannelState& channel : state->device.channels)
        {
            names.append(QString::fromStdString(channel.name));
        }
    }
    else if(state != nullptr)
    {
        names.append(QString::fromStdString(state->device.name.empty() ? std::string("Main") : state->device.name));
    }
    return JsonToJsValue(context, names, "<component-names>");
}

JSValue DeviceGetComponentDataJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    QJsonObject data;
    if(state == nullptr)
    {
        return JsonToJsValue(context, data, "<component-data>");
    }
    const std::string name = argc > 0 ? JsToString(context, argv[0]) : std::string();
    if(const RuntimeChannelState* channel = FindChannel(state->device, name))
    {
        data.insert("name", QString::fromStdString(channel->name));
        data.insert("LedCount", channel->led_count);
        data.insert("ledCount", channel->led_count);
    }
    else
    {
        data.insert("name", QString::fromStdString(!name.empty() ? name : (state->device.name.empty() ? std::string("Main") : state->device.name)));
        data.insert("LedCount", CurrentMainLedCount(state->device));
        data.insert("ledCount", CurrentMainLedCount(state->device));
    }
    return JsonToJsValue(context, data, "<component-data>");
}

JSValue DeviceAddChannelJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || argc < 1)
    {
        return JS_UNDEFINED;
    }
    const std::string name = JsToString(context, argv[0]);
    const int fallback_limit = argc > 1 ? JsToInt(context, argv[1], 0) : 0;
    const int led_limit = std::max(0, argc > 2 ? JsToInt(context, argv[2], fallback_limit) : fallback_limit);
    RuntimeChannelState* channel = FindChannel(state->device, name);
    if(channel != nullptr)
    {
        if(channel->led_limit != led_limit)
        {
            channel->led_limit = led_limit;
            if(led_limit > 0 && channel->led_count > led_limit)
            {
                channel->led_count = led_limit;
                ResizeFrameColors(channel->colors, led_limit);
            }
            state->device.topology_dirty = true;
        }
        return JS_UNDEFINED;
    }

    RuntimeChannelState next;
    next.name = name;
    next.led_limit = led_limit;
    state->device.channels.push_back(std::move(next));
    state->device.topology_dirty = true;
    return JS_UNDEFINED;
}

JSValue DeviceRemoveChannelJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc > 0)
    {
        const std::string name = JsToString(context, argv[0]);
        const auto before = state->device.channels.size();
        state->device.channels.erase(std::remove_if(state->device.channels.begin(), state->device.channels.end(), [&](const RuntimeChannelState& item) {
            return item.name == name;
        }), state->device.channels.end());
        if(state->device.channels.size() != before)
        {
            state->device.topology_dirty = true;
        }
    }
    return JS_UNDEFINED;
}

JSValue ChannelLedCountJs(JSContext* context, JSValueConst this_val, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    const RuntimeChannelState* channel = state != nullptr ? FindChannel(state->device, ThisChannelName(context, this_val)) : nullptr;
    return JS_NewInt32(context, channel != nullptr ? channel->led_count : 0);
}

JSValue ChannelLedLimitJs(JSContext* context, JSValueConst this_val, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    const RuntimeChannelState* channel = state != nullptr ? FindChannel(state->device, ThisChannelName(context, this_val)) : nullptr;
    return JS_NewInt32(context, channel != nullptr ? channel->led_limit : 0);
}

JSValue ChannelSetLedLimitJs(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    RuntimeChannelState* channel = state != nullptr ? FindChannel(state->device, ThisChannelName(context, this_val)) : nullptr;
    if(channel == nullptr || argc < 1)
    {
        return JS_NewInt32(context, 0);
    }
    const int limit = std::max(0, JsToInt(context, argv[0], 0));
    if(channel->led_limit != limit)
    {
        channel->led_limit = limit;
        if(limit > 0 && channel->led_count > limit)
        {
            channel->led_count = limit;
            ResizeFrameColors(channel->colors, limit);
        }
        state->device.topology_dirty = true;
    }
    return JS_NewInt32(context, channel->led_limit);
}

JSValue ChannelShouldPulseColorsJs(JSContext* context, JSValueConst this_val, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    const RuntimeChannelState* channel = state != nullptr ? FindChannel(state->device, ThisChannelName(context, this_val)) : nullptr;
    return JS_NewBool(context, channel == nullptr || channel->led_count == 0 || channel->needs_pulse);
}

JSValue ChannelGetColorsJs(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    const RuntimeChannelState* channel = state != nullptr ? FindChannel(state->device, ThisChannelName(context, this_val)) : nullptr;
    const std::string format = argc > 0 ? JsToString(context, argv[0]) : std::string();
    const std::string order = argc > 1 ? JsToString(context, argv[1], "RGB") : std::string("RGB");
    return ColorsToJs(context, channel != nullptr ? channel->colors : std::vector<std::uint8_t>(), format, order);
}

JSValue ChannelGetComponentNamesJs(JSContext* context, JSValueConst this_val, int, JSValueConst*)
{
    QJsonArray names;
    names.append(QString::fromStdString(ThisChannelName(context, this_val)));
    return JsonToJsValue(context, names, "<channel-component-names>");
}

JSValue ChannelGetComponentDataJs(JSContext* context, JSValueConst this_val, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    const std::string name = ThisChannelName(context, this_val);
    const RuntimeChannelState* channel = state != nullptr ? FindChannel(state->device, name) : nullptr;
    QJsonObject data;
    data.insert("name", QString::fromStdString(name));
    data.insert("LedCount", channel != nullptr ? channel->led_count : 0);
    data.insert("ledCount", channel != nullptr ? channel->led_count : 0);
    return JsonToJsValue(context, data, "<channel-component-data>");
}

void DefineGetter(JSContext* context, JSValue object, const char* name, JSCFunction* getter)
{
    JSAtom atom = JS_NewAtom(context, name);
    JS_DefinePropertyGetSet(
        context,
        object,
        atom,
        JS_NewCFunction(context, getter, name, 0),
        JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(context, atom);
}

JSValue DeviceChannelJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || argc < 1)
    {
        return JS_NULL;
    }
    const std::string name = JsToString(context, argv[0]);
    if(FindChannel(state->device, name) == nullptr)
    {
        return JS_NULL;
    }

    JSValue object = JS_NewObject(context);
    SetValueProperty(context, object, "__name", JS_NewString(context, name.c_str()));
    SetFunctionProperty(context, object, "LedCount", ChannelLedCountJs);
    SetFunctionProperty(context, object, "LedLimit", ChannelLedLimitJs);
    SetFunctionProperty(context, object, "SetLedLimit", ChannelSetLedLimitJs, 1);
    SetFunctionProperty(context, object, "shouldPulseColors", ChannelShouldPulseColorsJs);
    SetFunctionProperty(context, object, "getColors", ChannelGetColorsJs, 2);
    SetFunctionProperty(context, object, "getComponentNames", ChannelGetComponentNamesJs);
    SetFunctionProperty(context, object, "getComponentData", ChannelGetComponentDataJs);
    DefineGetter(context, object, "_ledCount", ChannelLedCountJs);
    DefineGetter(context, object, "ledCount", ChannelLedCountJs);
    DefineGetter(context, object, "_ledLimit", ChannelLedLimitJs);
    DefineGetter(context, object, "ledLimit", ChannelLedLimitJs);
    return object;
}

JSValue DeviceGetChannelNamesJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    QJsonArray names;
    if(state != nullptr)
    {
        for(const RuntimeChannelState& channel : state->device.channels)
        {
            names.append(QString::fromStdString(channel.name));
        }
    }
    return JsonToJsValue(context, names, "<channel-names>");
}

JSValue DeviceGetChannelPulseColorJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    const int phase = state != nullptr ? (state->device.pulse_phase = (state->device.pulse_phase + 5) % 360) : 0;
    const int intensity = static_cast<int>(128 + 127 * std::sin(phase * 3.14159265358979323846 / 180.0));
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#00%02X%02X", std::max(0, intensity / 2), std::clamp(intensity, 0, 255));
    return JS_NewString(context, buffer);
}

std::vector<std::uint8_t> ColorArrayFromHex(const std::string& color, int count)
{
    int r = 0;
    int g = 0;
    int b = 0;
    if(color.size() == 7 && color[0] == '#')
    {
        r = std::stoi(color.substr(1, 2), nullptr, 16);
        g = std::stoi(color.substr(3, 2), nullptr, 16);
        b = std::stoi(color.substr(5, 2), nullptr, 16);
    }
    std::vector<std::uint8_t> colors;
    colors.reserve(static_cast<std::size_t>(std::max(0, count)) * 3);
    for(int idx = 0; idx < count; idx++)
    {
        colors.push_back(static_cast<std::uint8_t>(r));
        colors.push_back(static_cast<std::uint8_t>(g));
        colors.push_back(static_cast<std::uint8_t>(b));
    }
    return colors;
}

JSValue DeviceCreateColorArrayJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    const std::string color = argc > 0 ? JsToString(context, argv[0]) : std::string();
    const int count = std::max(0, argc > 1 ? JsToInt(context, argv[1], 0) : 0);
    const std::string format = argc > 2 ? JsToString(context, argv[2]) : std::string();
    const std::string order = argc > 3 ? JsToString(context, argv[3], "RGB") : std::string("RGB");
    try
    {
        return ColorsToJs(context, ColorArrayFromHex(color, count), format, order);
    }
    catch(...)
    {
        return ColorsToJs(context, ColorArrayFromHex("", count), format, order);
    }
}

JSValue DeviceCreateSubdeviceJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc > 0)
    {
        const std::string name = JsToString(context, argv[0]);
        if(FindSubdevice(state->device, name) == nullptr)
        {
            RuntimeSubdeviceState subdevice;
            subdevice.name = name;
            subdevice.display_name = name;
            state->device.subdevices.push_back(std::move(subdevice));
            state->device.topology_dirty = true;
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceSetSubdeviceNameJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc >= 1)
    {
        RuntimeSubdeviceState* subdevice = FindSubdevice(state->device, JsToString(context, argv[0]));
        if(subdevice != nullptr)
        {
            const std::string display = argc > 1 ? JsToString(context, argv[1]) : subdevice->name;
            if(subdevice->display_name != display)
            {
                subdevice->display_name = display;
                state->device.topology_dirty = true;
            }
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceSetSubdeviceImageUrlJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc >= 1)
    {
        RuntimeSubdeviceState* subdevice = FindSubdevice(state->device, JsToString(context, argv[0]));
        if(subdevice != nullptr)
        {
            const std::string image = argc > 1 ? JsToString(context, argv[1]) : std::string();
            if(subdevice->image_url != image)
            {
                subdevice->image_url = image;
                state->device.topology_dirty = true;
            }
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceSetSubdeviceSizeJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc >= 3)
    {
        RuntimeSubdeviceState* subdevice = FindSubdevice(state->device, JsToString(context, argv[0]));
        if(subdevice != nullptr)
        {
            const int width = std::max(1, JsToInt(context, argv[1], 1));
            const int height = std::max(1, JsToInt(context, argv[2], 1));
            if(subdevice->width != width || subdevice->height != height)
            {
                subdevice->width = width;
                subdevice->height = height;
                state->device.topology_dirty = true;
            }
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceSetSubdeviceLedsJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc >= 1)
    {
        RuntimeSubdeviceState* subdevice = FindSubdevice(state->device, JsToString(context, argv[0]));
        if(subdevice != nullptr)
        {
            const std::vector<std::string> names = argc > 1 ? JsStringArray(context, argv[1]) : std::vector<std::string>();
            const std::vector<std::pair<int, int>> positions = argc > 2 ? JsPositionArray(context, argv[2]) : std::vector<std::pair<int, int>>();
            if(subdevice->led_names != names || subdevice->led_positions != positions)
            {
                subdevice->led_names = names;
                subdevice->led_positions = positions;
                RebuildSubdevicePositionIndex(*subdevice);
                ResizeFrameColors(subdevice->colors, SubdeviceLedCount(*subdevice));
                state->device.topology_dirty = true;
            }
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceRemoveSubdeviceJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && argc > 0)
    {
        const std::string name = JsToString(context, argv[0]);
        const auto before = state->device.subdevices.size();
        state->device.subdevices.erase(std::remove_if(state->device.subdevices.begin(), state->device.subdevices.end(), [&](const RuntimeSubdeviceState& item) {
            return item.name == name;
        }), state->device.subdevices.end());
        if(state->device.subdevices.size() != before)
        {
            state->device.topology_dirty = true;
        }
    }
    return JS_UNDEFINED;
}

JSValue DeviceGetCurrentSubdevicesJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    QJsonArray names;
    if(state != nullptr)
    {
        for(const RuntimeSubdeviceState& subdevice : state->device.subdevices)
        {
            names.append(QString::fromStdString(subdevice.name));
        }
    }
    return JsonToJsValue(context, names, "<subdevices>");
}

void RegisterConsole(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "log", LogJs);
    SetFunctionProperty(context, object, "info", LogJs);
    SetFunctionProperty(context, object, "warn", ConsoleWarnJs);
    SetFunctionProperty(context, object, "error", ConsoleErrorJs);
    SetGlobalProperty(context, "console", object);
}

void RegisterDevice(JSContext* context)
{
    JSValue device = JS_NewObject(context);
    SetFunctionProperty(context, device, "write", HidWriteJs, 2);
    SetFunctionProperty(context, device, "read", HidReadJs, 3);
    SetFunctionProperty(context, device, "send_report", HidSendReportJs, 2);
    SetFunctionProperty(context, device, "get_report", HidGetReportJs, 2);
    SetFunctionProperty(context, device, "input_report", HidInputReportJs, 2);
    SetFunctionProperty(context, device, "set_endpoint", HidSetEndpointJs, 3);
    SetFunctionProperty(context, device, "getHidEndpoints", HidGetEndpointsJs);
    SetFunctionProperty(context, device, "getHidInfo", DeviceGetHidInfoJs);
    SetFunctionProperty(context, device, "getDeviceInfo", DeviceGetDeviceInfoJs);
    SetFunctionProperty(context, device, "bulk_transfer", EmptyArrayJs);
    SetFunctionProperty(context, device, "control_transfer", EmptyArrayJs);
    SetFunctionProperty(context, device, "color", DeviceColorJs, 2);
    SetFunctionProperty(context, device, "subdeviceColor", DeviceSubdeviceColorJs, 3);
    SetFunctionProperty(context, device, "log", LogJs);
    SetFunctionProperty(context, device, "pause", PauseJs, 1);
    SetFunctionProperty(context, device, "getBrightness", HundredJs);
    SetFunctionProperty(context, device, "getLedCount", DeviceGetLedCountJs);
    SetFunctionProperty(context, device, "productId", DeviceProductIdJs);
    SetFunctionProperty(context, device, "flush", HidFlushJs);
    SetFunctionProperty(context, device, "clearReadBuffer", HidFlushJs);
    SetFunctionProperty(context, device, "getLastReadSize", HidLastReadSizeJs);
    SetFunctionProperty(context, device, "notify", UndefinedJs);
    SetFunctionProperty(context, device, "setFrameRateTarget", UndefinedJs);
    SetFunctionProperty(context, device, "addFeature", UndefinedJs);
    SetFunctionProperty(context, device, "SetLedLimit", DeviceSetLedLimitJs, 1);
    SetFunctionProperty(context, device, "SetTemperature", UndefinedJs);
    SetFunctionProperty(context, device, "setName", DeviceSetNameJs, 1);
    SetFunctionProperty(context, device, "setImageFromUrl", DeviceSetImageFromUrlJs, 1);
    SetFunctionProperty(context, device, "setSize", DeviceSetSizeJs, 1);
    SetFunctionProperty(context, device, "setControllableLeds", DeviceSetControllableLedsJs, 2);
    SetFunctionProperty(context, device, "addProperty", DeviceAddPropertyJs, 1);
    SetFunctionProperty(context, device, "getProperty", DeviceGetPropertyJs, 1);
    SetFunctionProperty(context, device, "removeProperty", DeviceRemovePropertyJs, 1);
    SetFunctionProperty(context, device, "getComponentNames", DeviceGetComponentNamesJs);
    SetFunctionProperty(context, device, "getComponentData", DeviceGetComponentDataJs, 1);
    SetFunctionProperty(context, device, "addChannel", DeviceAddChannelJs, 3);
    SetFunctionProperty(context, device, "removeChannel", DeviceRemoveChannelJs, 1);
    SetFunctionProperty(context, device, "channel", DeviceChannelJs, 1);
    SetFunctionProperty(context, device, "getChannelNames", DeviceGetChannelNamesJs);
    SetFunctionProperty(context, device, "getChannelPulseColor", DeviceGetChannelPulseColorJs);
    SetFunctionProperty(context, device, "createColorArray", DeviceCreateColorArrayJs, 4);
    SetFunctionProperty(context, device, "createSubdevice", DeviceCreateSubdeviceJs, 1);
    SetFunctionProperty(context, device, "setSubdeviceName", DeviceSetSubdeviceNameJs, 2);
    SetFunctionProperty(context, device, "setSubdeviceImageUrl", DeviceSetSubdeviceImageUrlJs, 2);
    SetFunctionProperty(context, device, "setSubdeviceSize", DeviceSetSubdeviceSizeJs, 3);
    SetFunctionProperty(context, device, "setSubdeviceLeds", DeviceSetSubdeviceLedsJs, 3);
    SetFunctionProperty(context, device, "removeSubdevice", DeviceRemoveSubdeviceJs, 1);
    SetFunctionProperty(context, device, "getCurrentSubdevices", DeviceGetCurrentSubdevicesJs);
    SetFunctionProperty(context, device, "createFanControl", UndefinedJs);
    SetFunctionProperty(context, device, "removeFanControl", UndefinedJs);
    SetFunctionProperty(context, device, "fanControlDisabled", UndefinedJs);
    SetFunctionProperty(context, device, "getFanlevel", ZeroJs);
    SetFunctionProperty(context, device, "getNormalizedFanlevel", ZeroJs);
    SetFunctionProperty(context, device, "setRPM", UndefinedJs);
    SetFunctionProperty(context, device, "createTemperatureSensor", UndefinedJs);
    SetFunctionProperty(context, device, "removeTemperatureSensor", UndefinedJs);
    SetFunctionProperty(context, device, "getImageBuffer", EmptyArrayJs);
    SetGlobalProperty(context, "device", device);
}

JSValue HostGetMotherboardInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JsonToJsValue(context, MotherboardInfo(), "<systeminfo-motherboard>");
}

JSValue HostGetBiosInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JsonToJsValue(context, BiosInfo(), "<systeminfo-bios>");
}

JSValue HostGetRamInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JsonToJsValue(context, RamInfo(), "<systeminfo-ram>");
}

JSValue HostGetHidInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return DeviceGetHidInfoJs(context, JS_UNDEFINED, 0, nullptr);
}

JSValue HostGetHidEndpointsJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return HidGetEndpointsJs(context, JS_UNDEFINED, 0, nullptr);
}

struct HostExport
{
    const char* name;
    JSCFunction* function;
    int argc;
};

constexpr HostExport kHostFunctions[] = {
    { "log", LogJs, 1 },
    { "pause", PauseJs, 1 },
    { "getMotherboardInfo", HostGetMotherboardInfoJs, 0 },
    { "getBiosInfo", HostGetBiosInfoJs, 0 },
    { "getRamInfo", HostGetRamInfoJs, 0 },
    { "getHidInfo", HostGetHidInfoJs, 0 },
    { "getHidEndpoints", HostGetHidEndpointsJs, 0 },
};

int HostModuleInit(JSContext* context, JSModuleDef* module)
{
    for(const HostExport& item : kHostFunctions)
    {
        if(JS_SetModuleExport(
               context,
               module,
               item.name,
               JS_NewCFunction(context, item.function, item.name, item.argc)) < 0)
        {
            return -1;
        }
    }
    return 0;
}
}

void RegisterRuntimeCallbacks(JSContext* context)
{
    RegisterConsole(context);
    RegisterSignalRgbPackageGlobals(context);
    RegisterSignalRgbEnvironmentGlobals(context);
    RegisterDevice(context);
}

bool IsSignalBridgeHostModule(const std::string& specifier)
{
    return specifier == kHostModuleName;
}

JSModuleDef* LoadSignalBridgeHostModule(JSContext* context, const char* module_name)
{
    JSModuleDef* module = JS_NewCModule(context, module_name, HostModuleInit);
    if(module == nullptr)
    {
        return nullptr;
    }
    for(const HostExport& item : kHostFunctions)
    {
        if(JS_AddModuleExport(context, module, item.name) < 0)
        {
            return nullptr;
        }
    }
    return module;
}

void RuntimeApplyStaticMetadata(RuntimeCallbackState& state, const ScriptMeta& meta)
{
    RuntimeDeviceState& device = state.device;
    device.width = std::max(1u, meta.width);
    device.height = std::max(1u, meta.height);
    device.led_names = meta.led_names;
    device.led_positions = meta.led_positions;
    device.led_count = static_cast<int>(std::max(device.led_names.size(), device.led_positions.size()));
    device.topology_dirty = true;
}

QJsonObject RuntimeTakeTopologyUpdate(RuntimeCallbackState& state, bool force)
{
    if(!force && !state.device.topology_dirty)
    {
        return QJsonObject();
    }
    state.device.topology_dirty = false;

    const RuntimeDeviceState& device = state.device;
    QJsonObject topology;
    topology.insert("name", QString::fromStdString(device.name));
    topology.insert("image_url", QString::fromStdString(device.image_url));
    topology.insert("total_led_limit", device.total_led_limit);

    QJsonObject main;
    main.insert("width", std::max(1, device.width));
    main.insert("height", std::max(1, device.height));
    main.insert("led_count", CurrentMainLedCount(device));
    main.insert("canvas_led_count", MainCanvasLedCount(device));
    main.insert("led_names", StringArrayToJson(device.led_names));
    main.insert("led_positions", PositionArrayToJson(device.led_positions));
    topology.insert("main", main);

    QJsonArray channels;
    for(const RuntimeChannelState& channel : device.channels)
    {
        QJsonObject item;
        item.insert("name", QString::fromStdString(channel.name));
        item.insert("led_count", channel.led_count);
        item.insert("led_limit", channel.led_limit);
        item.insert("needs_pulse", channel.needs_pulse);
        channels.append(item);
    }
    topology.insert("channels", channels);

    QJsonArray subdevices;
    for(const RuntimeSubdeviceState& subdevice : device.subdevices)
    {
        QJsonObject item;
        item.insert("name", QString::fromStdString(subdevice.name));
        item.insert("display_name", QString::fromStdString(subdevice.display_name.empty() ? subdevice.name : subdevice.display_name));
        item.insert("image_url", QString::fromStdString(subdevice.image_url));
        item.insert("width", std::max(1, subdevice.width));
        item.insert("height", std::max(1, subdevice.height));
        item.insert("led_count", SubdeviceLedCount(subdevice));
        item.insert("led_names", StringArrayToJson(subdevice.led_names));
        item.insert("led_positions", PositionArrayToJson(subdevice.led_positions));
        subdevices.append(item);
    }
    topology.insert("subdevices", subdevices);
    return topology;
}

QJsonArray RuntimeExportProperties(const RuntimeCallbackState& state)
{
    QJsonArray properties;
    for(const QJsonObject& property : state.properties)
    {
        properties.append(property);
    }
    return properties;
}
}
