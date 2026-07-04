#include "runtime/signalrgb/SignalRgbModuleRegistry.h"
#include "runtime/signalrgb/SignalRgbModuleUtils.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "runtime/RuntimeBindingUtils.h"
#include "runtime/RuntimeBindings.h"
#include "runtime/QuickJsValue.h"

namespace signalbridge
{
namespace
{
bool SameSerialPort(const SerialInfo& a, const SerialInfo& b)
{
    return !a.port_name.empty() && a.port_name == b.port_name;
}

bool HasSerialPort(const SerialInfo& info)
{
    return !info.port_name.empty();
}

SerialParity ParseParity(const QString& value)
{
    if(value.compare("Even", Qt::CaseInsensitive) == 0)
    {
        return SerialParity::Even;
    }
    if(value.compare("Odd", Qt::CaseInsensitive) == 0)
    {
        return SerialParity::Odd;
    }
    return SerialParity::None;
}

int ParseDataBits(int value)
{
    return std::clamp(value, 5, 8);
}

SerialStopBits ParseStopBits(const QString& value)
{
    if(value.compare("Two", Qt::CaseInsensitive) == 0)
    {
        return SerialStopBits::Two;
    }
    return SerialStopBits::One;
}

QJsonObject SerialInfoToJson(const SerialInfo& info)
{
    QJsonObject object;
    object.insert("portName", QString::fromStdString(info.port_name));
    object.insert("path", QString::fromStdString(info.system_location));
    object.insert("systemLocation", QString::fromStdString(info.system_location));
    object.insert("description", QString::fromStdString(info.description));
    object.insert("manufacturer", QString::fromStdString(info.manufacturer));
    object.insert("serial", QString::fromStdString(info.serial_number));
    object.insert("serialNumber", QString::fromStdString(info.serial_number));
    if(info.has_vid)
    {
        object.insert("vid", static_cast<int>(info.vid));
        object.insert("vendorId", static_cast<int>(info.vid));
    }
    if(info.has_pid)
    {
        object.insert("pid", static_cast<int>(info.pid));
        object.insert("productId", static_cast<int>(info.pid));
    }
    return object;
}

std::vector<SerialInfo> AvailablePortsForRuntime(RuntimeCallbackState* state)
{
    std::vector<SerialInfo> ports;
    if(state == nullptr || !state->serial_backend)
    {
        return ports;
    }

    if(HasSerialPort(state->primary_serial))
    {
        ports.push_back(state->primary_serial);
    }

    for(const SerialInfo& info : state->serial_backend->Enumerate())
    {
        const bool duplicate = std::any_of(ports.begin(), ports.end(), [&](const SerialInfo& existing) {
            return SameSerialPort(existing, info);
        });
        if(!duplicate)
        {
            ports.push_back(info);
        }
    }
    return ports;
}

SerialOptions ParseSerialOptions(JSContext* context, int argc, JSValueConst* argv)
{
    SerialOptions options;
    if(argc == 0 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]))
    {
        return options;
    }

    if(JS_IsString(argv[0]))
    {
        options.port_name = JsToString(context, argv[0]);
        return options;
    }

    const QJsonObject object = JsValueToJson(context, argv[0]).toObject();
    options.port_name = object.value("portName").toString(object.value("path").toString()).toStdString();
    options.baud_rate = std::max(1, object.value("baudRate").toInt(options.baud_rate));
    options.parity = ParseParity(object.value("parity").toString("None"));
    options.data_bits = ParseDataBits(object.value("dataBits").toInt(8));
    options.stop_bits = ParseStopBits(object.value("stopBits").toString("One"));
    return options;
}

std::optional<SerialInfo> ResolveSerialPort(RuntimeCallbackState* state, const std::string& port_name)
{
    if(state == nullptr || !state->serial_backend)
    {
        return std::nullopt;
    }

    if(!port_name.empty())
    {
        return state->serial_backend->FindPort(port_name);
    }

    if(HasSerialPort(state->primary_serial))
    {
        return state->primary_serial;
    }

    const std::vector<SerialInfo> ports = state->serial_backend->Enumerate();
    if(!ports.empty())
    {
        return ports.front();
    }
    return std::nullopt;
}

JSValue SerialAvailablePortsJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    QJsonArray array;
    for(const SerialInfo& info : AvailablePortsForRuntime(RuntimeCallbacks(context)))
    {
        array.append(SerialInfoToJson(info));
    }
    return JsonToJsValue(context, array, "<serial-ports>");
}

JSValue SerialGetDeviceInfoJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr)
    {
        return JS_NewObject(context);
    }

    std::optional<SerialInfo> info;
    if(argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
    {
        const std::string port_name = JsToString(context, argv[0]);
        if(state->serial_backend && !port_name.empty())
        {
            info = state->serial_backend->FindPort(port_name);
        }
    }
    else if(HasSerialPort(state->active_serial))
    {
        info = state->active_serial;
    }
    else if(HasSerialPort(state->primary_serial))
    {
        info = state->primary_serial;
    }

    return JsonToJsValue(context, info.has_value() ? SerialInfoToJson(*info) : QJsonObject(), "<serial-info>");
}

JSValue SerialConnectJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->serial_backend)
    {
        return JS_NewBool(context, false);
    }

    try
    {
        SerialOptions options = ParseSerialOptions(context, argc, argv);
        std::optional<SerialInfo> info = ResolveSerialPort(state, options.port_name);
        if(!info.has_value())
        {
            return JS_NewBool(context, false);
        }
        if(options.port_name.empty())
        {
            options.port_name = info->port_name;
        }

        if(state->serial_connection && state->serial_connection->IsOpen())
        {
            if(SameSerialPort(state->active_serial, *info))
            {
                return JS_NewBool(context, true);
            }
            state->serial_connection->Close();
            state->serial_connection.reset();
        }

        std::unique_ptr<SerialConnection> connection = state->serial_backend->Open(*info, options);
        if(!connection || !connection->IsOpen())
        {
            return JS_NewBool(context, false);
        }

        state->active_serial = *info;
        state->serial_connection = std::move(connection);
        return JS_NewBool(context, true);
    }
    catch(...)
    {
        return JS_NewBool(context, false);
    }
}

JSValue SerialDisconnectJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state != nullptr && state->serial_connection)
    {
        state->serial_connection->Close();
        state->serial_connection.reset();
    }
    return JS_UNDEFINED;
}

JSValue SerialIsConnectedJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    return JS_NewBool(context, state != nullptr && state->serial_connection && state->serial_connection->IsOpen());
}

JSValue SerialWriteJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->serial_connection || !state->serial_connection->IsOpen() || argc < 1)
    {
        return JS_NewInt32(context, 0);
    }

    try
    {
        const std::vector<std::uint8_t> bytes = JsValueToBytes(context, argv[0]);
        if(bytes.empty())
        {
            return JS_NewInt32(context, 0);
        }

        return JS_NewInt32(context, state->serial_connection->Write(bytes));
    }
    catch(...)
    {
        return JS_NewInt32(context, 0);
    }
}

JSValue SerialReadJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->serial_connection || !state->serial_connection->IsOpen())
    {
        return JS_NewArray(context);
    }

    try
    {
        const int max_bytes = argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])
                                  ? JsToInt(context, argv[0], -1)
                                  : -1;
        const int timeout_ms = argc > 1 ? std::max(0, JsToInt(context, argv[1], 1000)) : 1000;
        return BytesToJsArray(context, state->serial_connection->Read(max_bytes >= 0 ? max_bytes : 64, timeout_ms));
    }
    catch(...)
    {
        return JS_NewArray(context);
    }
}

JSValue SerialReadAllJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->serial_connection || !state->serial_connection->IsOpen())
    {
        return JS_NewArray(context);
    }
    return BytesToJsArray(context, state->serial_connection->Read(4096, 0));
}

JSValue SerialGetPortNameJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr)
    {
        return JS_NewString(context, "");
    }
    if(state->serial_connection && state->serial_connection->IsOpen())
    {
        return JS_NewString(context, state->serial_connection->Info().port_name.c_str());
    }
    return JS_NewString(context, state->active_serial.port_name.c_str());
}

JSValue SerialGetBaudRateJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    return JS_NewInt32(context, state != nullptr && state->serial_connection
                                    ? state->serial_connection->BaudRate()
                                    : 0);
}

JSValue CreateSerialObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "availablePorts", SerialAvailablePortsJs);
    SetFunctionProperty(context, object, "getDeviceInfo", SerialGetDeviceInfoJs, 1);
    SetFunctionProperty(context, object, "connect", SerialConnectJs, 1);
    SetFunctionProperty(context, object, "disconnect", SerialDisconnectJs);
    SetFunctionProperty(context, object, "isConnected", SerialIsConnectedJs);
    SetFunctionProperty(context, object, "write", SerialWriteJs, 1);
    SetFunctionProperty(context, object, "read", SerialReadJs, 2);
    SetFunctionProperty(context, object, "readAll", SerialReadAllJs);
    SetFunctionProperty(context, object, "getPortName", SerialGetPortNameJs);
    SetFunctionProperty(context, object, "getBaudRate", SerialGetBaudRateJs);
    return object;
}

int SerialModuleInit(JSContext* context, JSModuleDef* module)
{
    return ExportObjectModule(context, module, nullptr, "serial", CreateSerialObject);
}

JSModuleDef* LoadSerialModule(JSContext* context, const char* module_name)
{
    return LoadObjectModule(context, module_name, SerialModuleInit, "serial");
}

[[maybe_unused]] const bool kRegistered = RegisterSignalRgbModule({
    "@SignalRGB/serial",
    LoadSerialModule,
    nullptr,
});
}
}
