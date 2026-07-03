#include "runtime/BuiltinModules.h"

#include <stdexcept>

#include <QFile>
#include <QString>

namespace signalbridge
{
namespace
{
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
    GetBiosInfo: function() { return { vendor: "", version: "", date: "", releaseDate: "" }; },
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
}

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

std::string LoadRuntimeResourceText(const std::string& relative_path)
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
