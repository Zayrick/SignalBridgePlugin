#include "SignalBridgeScriptRuntime.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <QFile>
#include <QJsonDocument>

extern "C" {
#include "quickjs.h"
}

namespace
{
constexpr const char* kApplyStaticMetadataJs = R"JS(
(function() {
    if (typeof device === 'undefined') return;

    var size = (typeof __srgb_static_size !== 'undefined') ? __srgb_static_size : null;
    if (Array.isArray(size) && size.length >= 2) {
        device.setSize(size);
    }

    var names = (typeof __srgb_static_led_names !== 'undefined') ? __srgb_static_led_names : null;
    var positions = (typeof __srgb_static_led_positions !== 'undefined') ? __srgb_static_led_positions : null;
    if ((Array.isArray(names) && names.length > 0) || (Array.isArray(positions) && positions.length > 0)) {
        device.setControllableLeds(
            Array.isArray(names) ? names : [],
            Array.isArray(positions) ? positions : []
        );
    }
})();
)JS";

std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NormalizeLookupPath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');

    std::vector<std::string> parts;
    std::size_t start = 0;
    while(start <= path.size())
    {
        const std::size_t slash = path.find('/', start);
        const std::string part = slash == std::string::npos
                                     ? path.substr(start)
                                     : path.substr(start, slash - start);
        if(part.empty() || part == ".")
        {
        }
        else if(part == "..")
        {
            if(!parts.empty())
            {
                parts.pop_back();
            }
        }
        else
        {
            parts.push_back(part);
        }

        if(slash == std::string::npos)
        {
            break;
        }
        start = slash + 1;
    }

    std::string normalized;
    for(std::size_t idx = 0; idx < parts.size(); idx++)
    {
        if(idx > 0)
        {
            normalized += '/';
        }
        normalized += parts[idx];
    }
    return normalized;
}

std::string LookupDir(const std::string& path)
{
    const std::string normalized = NormalizeLookupPath(path);
    const std::size_t slash = normalized.find_last_of('/');
    return slash == std::string::npos ? std::string() : normalized.substr(0, slash);
}

std::string JoinLookupPath(const std::string& lhs, const std::string& rhs)
{
    return lhs.empty() ? rhs : lhs + "/" + rhs;
}

constexpr const char* kErrorsModuleJs = R"JS(
const ContextErrorExport = globalThis.ContextError || function ContextError(message) {
    this.message = message || "";
    this.name = "ContextError";
};
const AssertExport = globalThis.Assert || {
    isOk: function(value, message) { if (!value) throw new ContextErrorExport(message || "Assertion failed"); },
    fail: function(message) { throw new ContextErrorExport(message || "Assertion failed"); },
    unreachable: function(message) { throw new ContextErrorExport(message || "Unreachable"); },
    isEqual: function(a, b, message) { if (a !== b) throw new ContextErrorExport(message || "Assertion failed"); },
    softIsDefined: function(value) { return value !== undefined && value !== null; },
};
const globalContextExport = globalThis.globalContext || {};
const globalContextValues = globalThis.__signalBridgeGlobalContextValues || Object.create(null);
globalThis.__signalBridgeGlobalContextValues = globalContextValues;
const originalGlobalContextSet = typeof globalContextExport.set === "function" ? globalContextExport.set : null;
const originalGlobalContextGet = typeof globalContextExport.get === "function" ? globalContextExport.get : null;
globalContextExport.set = function(key, value) {
    globalContextValues[String(key)] = value;
    if (originalGlobalContextSet) {
        originalGlobalContextSet.call(this, key, value);
    }
};
globalContextExport.get = function(key) {
    if (originalGlobalContextGet) {
        const value = originalGlobalContextGet.call(this, key);
        if (value !== undefined) {
            return value;
        }
    }
    return globalContextValues[String(key)];
};
globalContextExport.has = function(key) {
    return Object.prototype.hasOwnProperty.call(globalContextValues, String(key));
};
globalContextExport.clear = function(key) {
    delete globalContextValues[String(key)];
};
globalThis.globalContext = globalContextExport;
export { AssertExport as Assert, ContextErrorExport as ContextError, globalContextExport as globalContext };
export default { Assert: AssertExport, ContextError: ContextErrorExport, globalContext: globalContextExport };
)JS";

constexpr const char* kDeviceDiscoveryModuleJs = R"JS(
const DeviceDiscoveryExport = globalThis.DeviceDiscovery || {
    foundVirtualDevice: function() {},
};
export { DeviceDiscoveryExport as DeviceDiscovery };
export default DeviceDiscoveryExport;
)JS";

constexpr const char* kPermissionsModuleJs = R"JS(
const permissionsExport = globalThis.permissions || {
    permissions: function() { return []; },
    setCallback: function() {},
};
export { permissionsExport as permissions };
export default permissionsExport;
)JS";

constexpr const char* kSystemInfoModuleJs = R"JS(
const systeminfoExport = globalThis.systeminfo || {
    GetMotherboardInfo: function() { return { model: "", manufacturer: "", product: "", vendor: "" }; },
    GetBiosInfo: function() { return { vendor: "", version: "", date: "" }; },
    GetRamInfo: function() { return { totalMemory: 0, modules: [] }; },
};
export { systeminfoExport as systeminfo };
export default systeminfoExport;
)JS";

constexpr const char* kLcdModuleJs = R"JS(
const LCDExport = globalThis.LCD || {
    initialize: function() {},
    getFrame: function() { return []; },
};
export { LCDExport as LCD };
export default LCDExport;
)JS";

constexpr const char* kSerialModuleJs = R"JS(
const serialExport = globalThis.serial || {
    availablePorts: function() { return []; },
    getDeviceInfo: function() { return {}; },
    connect: function() { return false; },
    disconnect: function() {},
    isConnected: function() { return false; },
    write: function() { return false; },
    read: function() { return []; },
};
export { serialExport as serial };
export default serialExport;
)JS";

std::string NormalizeBuiltinSpecifier(std::string specifier)
{
    if(specifier.size() > 3 && specifier.substr(specifier.size() - 3) == ".js")
    {
        specifier.resize(specifier.size() - 3);
    }
    return specifier;
}

const char* BuiltinModuleSource(const std::string& specifier)
{
    const std::string normalized = NormalizeBuiltinSpecifier(specifier);
    if(normalized == "@SignalRGB/Errors")
    {
        return kErrorsModuleJs;
    }
    if(normalized == "@SignalRGB/DeviceDiscovery")
    {
        return kDeviceDiscoveryModuleJs;
    }
    if(normalized == "@SignalRGB/permissions")
    {
        return kPermissionsModuleJs;
    }
    if(normalized == "@SignalRGB/systeminfo")
    {
        return kSystemInfoModuleJs;
    }
    if(normalized == "@SignalRGB/lcd")
    {
        return kLcdModuleJs;
    }
    if(normalized == "@SignalRGB/serial")
    {
        return kSerialModuleJs;
    }
    return nullptr;
}

std::string JsonValueToString(const QJsonValue& value)
{
    const QByteArray wrapped = QJsonDocument(QJsonArray{ value }).toJson(QJsonDocument::Compact);
    if(wrapped.size() < 2)
    {
        return "null";
    }
    return std::string(wrapped.constData() + 1, static_cast<std::size_t>(wrapped.size() - 2));
}

QJsonValue JsonValueFromString(const char* value)
{
    if(value == nullptr)
    {
        return QJsonValue();
    }

    const QByteArray wrapped = QByteArray("[") + value + "]";
    const QJsonDocument document = QJsonDocument::fromJson(wrapped);
    if(document.isArray() && !document.array().isEmpty())
    {
        return document.array().first();
    }
    return QJsonValue(QString::fromUtf8(value));
}

bool JsonBool(const QJsonValue& value, bool fallback)
{
    if(value.isBool())
    {
        return value.toBool();
    }
    if(value.isString())
    {
        const QString text = value.toString().trimmed().toLower();
        if(text == "true" || text == "1" || text == "yes" || text == "on")
        {
            return true;
        }
        if(text == "false" || text == "0" || text == "no" || text == "off")
        {
            return false;
        }
    }
    return fallback;
}

double JsonNumber(const QJsonValue& value, double fallback)
{
    if(value.isDouble())
    {
        return value.toDouble();
    }
    if(value.isString())
    {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if(ok)
        {
            return parsed;
        }
    }
    return fallback;
}

QString JsonText(const QJsonValue& value, const QString& fallback = QString())
{
    if(value.isString())
    {
        return value.toString();
    }
    if(value.isBool())
    {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if(value.isDouble())
    {
        return QString::number(value.toDouble());
    }
    return fallback;
}

QJsonValue DefaultParameterValue(const QJsonObject& parameter)
{
    const QString type = parameter.value("type").toString().toLower();
    const QJsonValue fallback = parameter.value("default");

    if(type == "boolean" || type == "checkbox")
    {
        return JsonBool(fallback, false);
    }
    if(type == "number")
    {
        return JsonNumber(fallback, 0.0);
    }
    if(type == "combobox" || type == "select")
    {
        const QJsonArray values = parameter.value("values").toArray();
        if(fallback.isString())
        {
            return fallback.toString();
        }
        return values.isEmpty() ? QString() : JsonText(values.first());
    }
    return JsonText(fallback);
}

QJsonValue NormalizeParameterValue(const QJsonObject& parameter, const QJsonValue& value)
{
    const QString type = parameter.value("type").toString().toLower();
    const QJsonValue fallback = DefaultParameterValue(parameter);

    if(value.isUndefined() || value.isNull())
    {
        return fallback;
    }
    if(type == "boolean" || type == "checkbox")
    {
        return JsonBool(value, fallback.toBool(false));
    }
    if(type == "number")
    {
        return JsonNumber(value, fallback.toDouble(0.0));
    }
    if(type == "combobox" || type == "select")
    {
        const QString text = JsonText(value, fallback.toString());
        const QJsonArray values = parameter.value("values").toArray();
        if(values.isEmpty())
        {
            return text;
        }
        for(const QJsonValue& candidate : values)
        {
            if(JsonText(candidate) == text)
            {
                return text;
            }
        }
        return fallback;
    }
    return JsonText(value, fallback.toString());
}

JSValue JsonToJsValue(JSContext* context, const QJsonValue& value, const std::string& name)
{
    const std::string json = JsonValueToString(value);
    JSValue parsed = JS_ParseJSON(context, json.c_str(), json.size(), name.c_str());
    if(JS_IsException(parsed))
    {
        return parsed;
    }
    return parsed;
}

QJsonValue JsValueToJson(JSContext* context, JSValueConst value)
{
    if(JS_IsUndefined(value) || JS_IsNull(value))
    {
        return QJsonValue();
    }
    if(JS_IsBool(value))
    {
        return QJsonValue(JS_ToBool(context, value) != 0);
    }
    if(JS_IsNumber(value))
    {
        double number = 0.0;
        if(JS_ToFloat64(context, &number, value) == 0)
        {
            return QJsonValue(number);
        }
    }
    if(JS_IsString(value))
    {
        const char* str = JS_ToCString(context, value);
        const QString result = QString::fromUtf8(str != nullptr ? str : "");
        if(str != nullptr)
        {
            JS_FreeCString(context, str);
        }
        return result;
    }

    JSValue json_string = JS_JSONStringify(context, value, JS_UNDEFINED, JS_UNDEFINED);
    if(JS_IsException(json_string) || JS_IsUndefined(json_string))
    {
        JS_FreeValue(context, json_string);
        return QJsonValue();
    }

    const char* str = JS_ToCString(context, json_string);
    QJsonValue result = JsonValueFromString(str);
    if(str != nullptr)
    {
        JS_FreeCString(context, str);
    }
    JS_FreeValue(context, json_string);
    return result;
}

std::vector<std::uint8_t> JsValueToBytes(JSContext* context, JSValueConst value)
{
    const QJsonValue json = JsValueToJson(context, value);
    const QJsonArray array = json.toArray();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(array.size()));
    for(const QJsonValue& item : array)
    {
        const int clamped = std::clamp(item.toInt(0), 0, 255);
        bytes.push_back(static_cast<std::uint8_t>(clamped));
    }
    return bytes;
}

JSValue BytesToJsArray(JSContext* context, const std::vector<std::uint8_t>& bytes)
{
    JSValue array = JS_NewArray(context);
    for(std::size_t idx = 0; idx < bytes.size(); idx++)
    {
        JS_SetPropertyUint32(context, array, static_cast<std::uint32_t>(idx), JS_NewInt32(context, bytes[idx]));
    }
    return array;
}

int JsToInt(JSContext* context, JSValueConst value, int fallback)
{
    int32_t result = fallback;
    if(JS_ToInt32(context, &result, value) != 0)
    {
        return fallback;
    }
    return result;
}

std::size_t JsToSize(JSContext* context, JSValueConst value, std::size_t fallback)
{
    uint32_t result = static_cast<uint32_t>(fallback);
    if(JS_ToUint32(context, &result, value) != 0)
    {
        return fallback;
    }
    return static_cast<std::size_t>(result);
}

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

SignalBridgeHidBackend::Handle FindEndpointHandle(
    const std::map<std::string, SignalBridgeHidBackend::Handle>& handles,
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

    std::vector<SignalBridgeHidBackend::Handle> matched;
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

SignalBridgeJsCallbackState* CallbackState(JSContext* context)
{
    return static_cast<SignalBridgeJsCallbackState*>(JS_GetContextOpaque(context));
}
}

struct SignalBridgeJsCallbackState
{
    std::shared_ptr<SignalBridgeHidBackend> hid_backend;
    SignalBridgeHidBackend::Handle active_handle = 0;
    std::map<std::string, SignalBridgeHidBackend::Handle> endpoint_handles;
    std::vector<SignalBridgeEndpointDescriptor> endpoints;
    std::size_t last_read_size = 0;
    std::string script_name;
};

struct SignalBridgeModuleLoaderState
{
    std::vector<SignalBridgeScriptSource> catalog;
    std::vector<SignalBridgeScriptSource> loaded_modules;
    std::set<std::string> loaded_keys;

    void SetCatalog(const std::vector<SignalBridgeScriptSource>& sources)
    {
        catalog = sources;
        loaded_modules.clear();
        loaded_keys.clear();
    }

    void BeginLoad()
    {
        loaded_modules.clear();
        loaded_keys.clear();
    }

    const SignalBridgeScriptSource* Find(const std::string& lookup_path) const
    {
        const std::string normalized = NormalizeLookupPath(lookup_path);
        const std::string normalized_with_js = normalized.size() > 3 &&
                                                       normalized.substr(normalized.size() - 3) == ".js"
                                                   ? normalized
                                                   : normalized + ".js";
        const std::string key = LowerAscii(normalized);
        const std::string key_with_js = LowerAscii(normalized_with_js);
        for(const SignalBridgeScriptSource& source : catalog)
        {
            const std::string candidate = LowerAscii(NormalizeLookupPath(source.lookup_path));
            if(candidate == key || candidate == key_with_js)
            {
                return &source;
            }
        }
        return nullptr;
    }

    std::string Resolve(const std::string& base_name, const std::string& module_name) const
    {
        if(!module_name.empty() && module_name.front() == '.')
        {
            const std::string candidate = NormalizeLookupPath(JoinLookupPath(LookupDir(base_name), module_name));
            if(const SignalBridgeScriptSource* source = Find(candidate))
            {
                return NormalizeLookupPath(source->lookup_path);
            }
            return candidate;
        }

        if(BuiltinModuleSource(module_name) != nullptr)
        {
            return NormalizeBuiltinSpecifier(module_name);
        }

        if(const SignalBridgeScriptSource* source = Find(module_name))
        {
            return NormalizeLookupPath(source->lookup_path);
        }
        return module_name;
    }

    void RecordLoaded(const SignalBridgeScriptSource& source)
    {
        const std::string key = LowerAscii(NormalizeLookupPath(source.lookup_path));
        if(loaded_keys.insert(key).second)
        {
            loaded_modules.push_back(source);
        }
    }
};

struct SignalBridgeJsModuleState
{
    JSValue module = JS_UNDEFINED;
    JSValue namespace_object = JS_UNDEFINED;
    JSModuleDef* module_def = nullptr;
    bool evaluated = false;
};

namespace
{
JSModuleDef* CompileModule(JSContext* context, const std::string& name, const std::string& source)
{
    JSValue compiled = JS_Eval(
        context,
        source.c_str(),
        source.size(),
        name.c_str(),
        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if(JS_IsException(compiled))
    {
        return nullptr;
    }

    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    JS_FreeValue(context, compiled);
    return module;
}

char* ModuleNormalizeJs(JSContext* context, const char* module_base_name, const char* module_name, void* opaque)
{
    auto* state = static_cast<SignalBridgeModuleLoaderState*>(opaque);
    if(state == nullptr || module_name == nullptr)
    {
        return nullptr;
    }

    const std::string base = module_base_name != nullptr ? module_base_name : "";
    const std::string normalized = state->Resolve(base, module_name);
    if(!module_name[0] || (module_name[0] == '.' && state->Find(normalized) == nullptr))
    {
        JS_ThrowReferenceError(context, "relative import not found: %s from %s", module_name, base.c_str());
        return nullptr;
    }
    return js_strdup(context, normalized.c_str());
}

JSModuleDef* ModuleLoadJs(JSContext* context, const char* module_name, void* opaque)
{
    auto* state = static_cast<SignalBridgeModuleLoaderState*>(opaque);
    if(state == nullptr || module_name == nullptr)
    {
        JS_ThrowReferenceError(context, "module loader is not initialized");
        return nullptr;
    }

    const std::string name = module_name;
    if(const char* builtin = BuiltinModuleSource(name))
    {
        return CompileModule(context, name, builtin);
    }

    const SignalBridgeScriptSource* source = state->Find(name);
    if(source == nullptr)
    {
        JS_ThrowReferenceError(context, "could not load module '%s'", module_name);
        return nullptr;
    }

    state->RecordLoaded(*source);
    return CompileModule(context, NormalizeLookupPath(source->lookup_path), source->source);
}

JSValue HidWriteJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    SignalBridgeJsCallbackState* state = CallbackState(context);
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
    SignalBridgeJsCallbackState* state = CallbackState(context);
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
    SignalBridgeJsCallbackState* state = CallbackState(context);
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
    SignalBridgeJsCallbackState* state = CallbackState(context);
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
    SignalBridgeJsCallbackState* state = CallbackState(context);
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

    const SignalBridgeHidBackend::Handle handle = FindEndpointHandle(
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
    SignalBridgeJsCallbackState* state = CallbackState(context);
    QJsonArray endpoints;
    if(state != nullptr)
    {
        for(const SignalBridgeEndpointDescriptor& endpoint : state->endpoints)
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
    SignalBridgeJsCallbackState* state = CallbackState(context);
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
    SignalBridgeJsCallbackState* state = CallbackState(context);
    return JS_NewInt32(context, static_cast<int>(state != nullptr ? state->last_read_size : 0));
}

JSValue LogJs(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if(argc > 0)
    {
        const char* str = JS_ToCString(context, argv[0]);
        if(str != nullptr)
        {
            fprintf(stderr, "[SignalBridge] %s\n", str);
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
std::string SignalBridgeLoadReferenceTextFile(const std::string& relative_path)
{
    const QString path = QStringLiteral(":/SignalBridge/") + QString::fromStdString(relative_path);
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        throw std::runtime_error("Failed to read " + path.toStdString());
    }
    return QString::fromUtf8(file.readAll()).toStdString();
}

}

SignalBridgeJsRuntime::SignalBridgeJsRuntime()
    : SignalBridgeJsRuntime(true)
{
}

SignalBridgeJsRuntime::SignalBridgeJsRuntime(bool create_context)
{
    module_state_ = std::make_unique<SignalBridgeJsModuleState>();
    module_loader_state_ = std::make_unique<SignalBridgeModuleLoaderState>();

    if(!create_context)
    {
        return;
    }

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

    JS_SetModuleLoaderFunc(runtime_, ModuleNormalizeJs, ModuleLoadJs, module_loader_state_.get());
}

SignalBridgeJsRuntime::~SignalBridgeJsRuntime()
{
    Reset();
}

SignalBridgeJsRuntime::SignalBridgeJsRuntime(SignalBridgeJsRuntime&& other) noexcept
{
    *this = std::move(other);
}

SignalBridgeJsRuntime& SignalBridgeJsRuntime::operator=(SignalBridgeJsRuntime&& other) noexcept
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
            JS_SetModuleLoaderFunc(runtime_, ModuleNormalizeJs, ModuleLoadJs, module_loader_state_.get());
        }
    }
    return *this;
}

SignalBridgeJsRuntime SignalBridgeJsRuntime::CreateScan()
{
    SignalBridgeJsRuntime runtime;
    runtime.Eval(SignalBridgeLoadReferenceTextFile("js/scan_stubs.js"), "<scan-stubs>");
    runtime.Eval(SignalBridgeLoadReferenceTextFile("js/device.js"), "<scan-device>");
    return runtime;
}

SignalBridgeJsRuntime SignalBridgeJsRuntime::CreateValidation(const SignalBridgeScriptMeta& meta)
{
    SignalBridgeJsRuntime runtime = CreateScan();
    runtime.LoadModule(meta.lookup_path, meta.module_sources);
    runtime.EvaluateModule();
    runtime.ApplyConfiguration(meta, QJsonObject());
    return runtime;
}

SignalBridgeJsRuntime SignalBridgeJsRuntime::CreateRuntime(
    std::shared_ptr<SignalBridgeHidBackend> hid_backend,
    const SignalBridgeScriptMeta& meta,
    SignalBridgeHidBackend::Handle primary_handle,
    const SignalBridgeHidInfo& primary_hid,
    std::map<std::string, SignalBridgeHidBackend::Handle> endpoint_handles,
    std::vector<SignalBridgeEndpointDescriptor> endpoints,
    QJsonObject configuration)
{
    SignalBridgeJsRuntime runtime;
    runtime.callback_state_ = std::make_unique<SignalBridgeJsCallbackState>();
    runtime.callback_state_->hid_backend = std::move(hid_backend);
    runtime.callback_state_->active_handle = primary_handle;
    runtime.callback_state_->endpoint_handles = std::move(endpoint_handles);
    runtime.callback_state_->endpoints = std::move(endpoints);
    runtime.callback_state_->script_name = meta.name;
    JS_SetContextOpaque(runtime.context_, runtime.callback_state_.get());
    runtime.RegisterCallbacks();
    runtime.Eval(SignalBridgeLoadReferenceTextFile("js/polyfills.js"), "<polyfills>");
    runtime.Eval(SignalBridgeLoadReferenceTextFile("js/device.js"), "<device>");
    runtime.Eval("device._vid = " + std::to_string(primary_hid.vid) +
                     "; device._pid = " + std::to_string(primary_hid.pid) + ";",
                 "<hid-info>");
    runtime.ApplyStaticMetadata(meta);
    runtime.LoadModule(meta.lookup_path, meta.module_sources);
    runtime.EvaluateModule();
    runtime.ApplyConfigurationValues(configuration);
    runtime.ApplyConfiguration(meta, configuration);
    return runtime;
}

void SignalBridgeJsRuntime::Eval(const std::string& source, const std::string& name)
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

void SignalBridgeJsRuntime::LoadModule(
    const std::string& lookup_path,
    const std::vector<SignalBridgeScriptSource>& catalog)
{
    if(module_state_ == nullptr)
    {
        module_state_ = std::make_unique<SignalBridgeJsModuleState>();
    }
    if(module_loader_state_ == nullptr)
    {
        module_loader_state_ = std::make_unique<SignalBridgeModuleLoaderState>();
        JS_SetModuleLoaderFunc(runtime_, ModuleNormalizeJs, ModuleLoadJs, module_loader_state_.get());
    }

    ClearModuleState();
    module_loader_state_->SetCatalog(catalog);
    module_loader_state_->BeginLoad();

    const SignalBridgeScriptSource* source = module_loader_state_->Find(lookup_path);
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
    module_state_->evaluated = false;
}

void SignalBridgeJsRuntime::EvaluateModule()
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
    module_state_->evaluated = true;
}

bool SignalBridgeJsRuntime::HasGlobal(const std::string& name) const
{
    JSValue global = JS_GetGlobalObject(context_);
    JSValue value = JS_GetPropertyStr(context_, global, name.c_str());
    const bool found = !JS_IsException(value) && !JS_IsUndefined(value);
    JS_FreeValue(context_, value);
    JS_FreeValue(context_, global);
    return found;
}

bool SignalBridgeJsRuntime::HasModuleExport(const std::string& name)
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

QJsonValue SignalBridgeJsRuntime::CallGlobalJson(const std::string& name, const QJsonArray& args)
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

QJsonValue SignalBridgeJsRuntime::CallModuleExportJson(const std::string& name, const QJsonArray& args)
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

void SignalBridgeJsRuntime::SetGlobalJson(const std::string& name, const QJsonValue& value)
{
    JSValue global = JS_GetGlobalObject(context_);
    JSValue js_value = JsonToJsValue(context_, value, name);
    if(JS_IsException(js_value))
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, js_value);
        JS_FreeValue(context_, global);
        throw std::runtime_error(error);
    }
    if(JS_SetPropertyStr(context_, global, name.c_str(), js_value) < 0)
    {
        const std::string error = FormatException();
        JS_FreeValue(context_, global);
        throw std::runtime_error(error);
    }
    JS_FreeValue(context_, global);
}

void SignalBridgeJsRuntime::ApplyConfigurationValues(const QJsonObject& configuration)
{
    for(auto it = configuration.begin(); it != configuration.end(); ++it)
    {
        SetGlobalJson(it.key().toStdString(), it.value());
    }
}

void SignalBridgeJsRuntime::ApplyConfiguration(const SignalBridgeScriptMeta& meta, const QJsonObject& configuration)
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

std::vector<SignalBridgeScriptSource> SignalBridgeJsRuntime::LoadedModuleSources() const
{
    return module_loader_state_ != nullptr ? module_loader_state_->loaded_modules
                                           : std::vector<SignalBridgeScriptSource>();
}

void SignalBridgeJsRuntime::ClearModuleState()
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
    module_state_->evaluated = false;
}

void SignalBridgeJsRuntime::Reset()
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

void SignalBridgeJsRuntime::RegisterCallbacks()
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

    JSValue global = JS_GetGlobalObject(context_);
    for(const Callback& callback : callbacks)
    {
        JSValue function = JS_NewCFunction(context_, callback.function, callback.name, callback.argc);
        if(JS_SetPropertyStr(context_, global, callback.name, function) < 0)
        {
            JS_FreeValue(context_, global);
            throw std::runtime_error(FormatException());
        }
    }
    JS_FreeValue(context_, global);
}

void SignalBridgeJsRuntime::ApplyStaticMetadata(const SignalBridgeScriptMeta& meta)
{
    QJsonArray size;
    size.append(static_cast<int>(meta.width));
    size.append(static_cast<int>(meta.height));
    SetGlobalJson("__srgb_static_size", size);

    QJsonArray names;
    for(const std::string& name : meta.led_names)
    {
        names.append(QString::fromStdString(name));
    }
    SetGlobalJson("__srgb_static_led_names", names);

    QJsonArray positions;
    for(const auto& position : meta.led_positions)
    {
        QJsonArray item;
        item.append(position.first);
        item.append(position.second);
        positions.append(item);
    }
    SetGlobalJson("__srgb_static_led_positions", positions);
    Eval(kApplyStaticMetadataJs, "<static-metadata>");
}

std::string SignalBridgeJsRuntime::FormatException() const
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
