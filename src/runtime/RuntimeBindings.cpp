#include "runtime/RuntimeBindings.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>
#include <QVariant>

#include "runtime/QuickJsValue.h"

namespace signalbridge
{
RuntimeCallbackState* RuntimeCallbacks(JSContext* context)
{
    return static_cast<RuntimeCallbackState*>(JS_GetContextOpaque(context));
}

namespace
{
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

QString ReadSettingsString(QSettings& settings, const char* key)
{
    const QVariant value = settings.value(QString::fromLatin1(key));
    if(!value.isValid() || value.isNull())
    {
        return QString();
    }

    QString text = value.toString().trimmed();
    if(!text.isEmpty())
    {
        return text;
    }

    const QStringList values = value.toStringList();
    for(const QString& item : values)
    {
        const QString trimmed = item.trimmed();
        if(!trimmed.isEmpty())
        {
            if(!text.isEmpty())
            {
                text += QStringLiteral(" ");
            }
            text += trimmed;
        }
    }
    return text;
}

QString NormalizeBiosDate(const QString& raw)
{
    const QString text = raw.trimmed();
    if(text.isEmpty())
    {
        return QString();
    }

    if(text.size() >= 8)
    {
        bool digits = true;
        for(int idx = 0; idx < 8; idx++)
        {
            if(!text.at(idx).isDigit())
            {
                digits = false;
                break;
            }
        }
        if(digits)
        {
            const QDate date = QDate::fromString(text.left(8), QStringLiteral("yyyyMMdd"));
            if(date.isValid())
            {
                return date.toString(Qt::ISODate);
            }
        }
    }

    const QStringList formats = {
        QStringLiteral("MM/dd/yyyy"),
        QStringLiteral("M/d/yyyy"),
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd.MM.yyyy"),
    };
    for(const QString& format : formats)
    {
        const QDate date = QDate::fromString(text, format);
        if(date.isValid())
        {
            return date.toString(Qt::ISODate);
        }
    }
    return text;
}

QJsonObject MotherboardInfo()
{
    QString manufacturer;
    QString product;
    QString version;

#ifdef Q_OS_WIN
    QSettings bios(
        QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"),
        QSettings::NativeFormat);
    manufacturer = ReadSettingsString(bios, "BaseBoardManufacturer");
    product = ReadSettingsString(bios, "BaseBoardProduct");
    version = ReadSettingsString(bios, "BaseBoardVersion");
#endif

    QJsonObject info;
    info.insert(QStringLiteral("model"), product);
    info.insert(QStringLiteral("manufacturer"), manufacturer);
    info.insert(QStringLiteral("product"), product);
    info.insert(QStringLiteral("vendor"), manufacturer);
    if(!version.isEmpty())
    {
        info.insert(QStringLiteral("version"), version);
    }
    return info;
}

QJsonObject BiosInfo()
{
    QString vendor;
    QString version;
    QString raw_date;

#ifdef Q_OS_WIN
    QSettings bios(
        QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"),
        QSettings::NativeFormat);
    vendor = ReadSettingsString(bios, "BIOSVendor");
    version = ReadSettingsString(bios, "BIOSVersion");
    raw_date = ReadSettingsString(bios, "BIOSReleaseDate");
#endif

    const QString release_date = NormalizeBiosDate(raw_date);
    QJsonObject info;
    info.insert(QStringLiteral("vendor"), vendor);
    info.insert(QStringLiteral("version"), version);
    info.insert(QStringLiteral("date"), raw_date);
    info.insert(QStringLiteral("releaseDate"), release_date);
    return info;
}

QJsonObject RamInfo()
{
    QJsonObject info;
    info.insert(QStringLiteral("totalMemory"), 0);
    info.insert(QStringLiteral("modules"), QJsonArray());
    return info;
}

JSValue GetMotherboardInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JsonToJsValue(context, MotherboardInfo(), "<systeminfo-motherboard>");
}

JSValue GetBiosInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JsonToJsValue(context, BiosInfo(), "<systeminfo-bios>");
}

JSValue GetRamInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JsonToJsValue(context, RamInfo(), "<systeminfo-ram>");
}

void SetFunctionProperty(JSContext* context, JSValue object, const char* name, JSCFunction* function)
{
    JSValue js_function = JS_NewCFunction(context, function, name, 0);
    if(JS_SetPropertyStr(context, object, name, js_function) < 0)
    {
        throw std::runtime_error(std::string("failed to register systeminfo.") + name);
    }
}

void RegisterSystemInfo(JSContext* context)
{
    JSValue systeminfo = JS_NewObject(context);
    if(JS_IsException(systeminfo))
    {
        throw std::runtime_error("failed to create systeminfo object");
    }

    try
    {
        SetFunctionProperty(context, systeminfo, "GetMotherboardInfo", GetMotherboardInfoJs);
        SetFunctionProperty(context, systeminfo, "GetBiosInfo", GetBiosInfoJs);
        SetFunctionProperty(context, systeminfo, "GetRamInfo", GetRamInfoJs);
    }
    catch(...)
    {
        JS_FreeValue(context, systeminfo);
        throw;
    }

    JSValue global = JS_GetGlobalObject(context);
    if(JS_SetPropertyStr(context, global, "systeminfo", systeminfo) < 0)
    {
        JS_FreeValue(context, global);
        throw std::runtime_error("failed to register systeminfo object");
    }
    JS_FreeValue(context, global);
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

JSValue HidWriteJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->hid_backend || argc < 1)
    {
        return JS_NewInt32(context, 0);
    }
    try
    {
        const std::vector<std::uint8_t> bytes = JsValueToBytes(context, argv[0]);
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
    if(state == nullptr || !state->hid_backend)
    {
        return JS_NewArray(context);
    }
    try
    {
        const std::size_t length = argc > 0 ? JsToSize(context, argv[0], 64) : 64;
        const int timeout_ms = argc > 1 ? JsToInt(context, argv[1], 0) : 0;
        std::vector<std::uint8_t> bytes = state->hid_backend->Read(state->active_handle, length, timeout_ms);
        state->last_read_size = bytes.size();
        return BytesToJsArray(context, bytes);
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
    if(state == nullptr || !state->hid_backend || argc < 1)
    {
        return JS_NewInt32(context, 0);
    }
    try
    {
        const std::vector<std::uint8_t> bytes = JsValueToBytes(context, argv[0]);
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
    if(state == nullptr || !state->hid_backend)
    {
        return JS_NewArray(context);
    }
    try
    {
        const int report_id = std::clamp(argc > 0 ? JsToInt(context, argv[0], 0) : 0, 0, 255);
        const std::size_t length = argc > 1 ? JsToSize(context, argv[1], 64) : 64;
        std::vector<std::uint8_t> bytes = state->hid_backend->GetFeatureReport(
            state->active_handle,
            static_cast<std::uint8_t>(report_id),
            length);
        state->last_read_size = bytes.size();
        return BytesToJsArray(context, bytes);
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
    if(state == nullptr || argc < 3)
    {
        return JS_NewBool(context, false);
    }

    const int interface_number = JsToInt(context, argv[0], -1);
    const int usage = JsToInt(context, argv[1], -1);
    const int usage_page = JsToInt(context, argv[2], -1);
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
    QJsonArray endpoints;
    if(state != nullptr)
    {
        for(const EndpointDescriptor& endpoint : state->endpoints)
        {
            QJsonObject object;
            object.insert("interface", endpoint.interface_number);
            object.insert("usage", endpoint.usage);
            object.insert("usage_page", endpoint.usage_page);
            object.insert("collection", 0);
            endpoints.append(object);
        }
    }
    return JsonToJsValue(context, endpoints, "<endpoints>");
}

JSValue HidFlushJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    RuntimeCallbackState* state = RuntimeCallbacks(context);
    if(state == nullptr || !state->hid_backend)
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
    if(argc > 0)
    {
        const char* str = JS_ToCString(context, argv[0]);
        if(str != nullptr)
        {
            RuntimeCallbackState* state = RuntimeCallbacks(context);
            const std::string source = state != nullptr && !state->script_name.empty()
                                           ? state->script_name
                                           : std::string("script");
            const std::string message = str;
            std::fprintf(stderr, "[SignalBridge][%s] %s\n", source.c_str(), message.c_str());
            if(state != nullptr && state->log_callback)
            {
                state->log_callback(source, message);
            }
            JS_FreeCString(context, str);
        }
    }
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
}

void RegisterRuntimeCallbacks(JSContext* context)
{
    struct Callback
    {
        const char* name;
        JSCFunction* function;
        int argc;
    };

    const Callback callbacks[] = {
        { "_hid_write", HidWriteJs, 1 },
        { "_hid_read", HidReadJs, 2 },
        { "_hid_send_report", HidSendReportJs, 1 },
        { "_hid_get_report", HidGetReportJs, 2 },
        { "_hid_set_endpoint", HidSetEndpointJs, 3 },
        { "_hid_get_endpoints", HidGetEndpointsJs, 0 },
        { "_hid_flush", HidFlushJs, 0 },
        { "_hid_get_last_read_size", HidLastReadSizeJs, 0 },
        { "_log", LogJs, 1 },
        { "_pause", PauseJs, 1 },
    };

    JSValue global = JS_GetGlobalObject(context);
    for(const Callback& callback : callbacks)
    {
        JSValue function = JS_NewCFunction(context, callback.function, callback.name, callback.argc);
        if(JS_SetPropertyStr(context, global, callback.name, function) < 0)
        {
            JS_FreeValue(context, global);
            throw std::runtime_error("failed to register runtime callback");
        }
    }
    JS_FreeValue(context, global);

    RegisterSystemInfo(context);
}
}
