#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "filesystem.h"
#include "config/DeviceConfigStore.h"
#include "domain/ControlParameters.h"
#include "domain/PathUtils.h"
#include "domain/ScriptTypes.h"
#include "openrgb/FrameBuilder.h"
#include "openrgb/TopologyMapper.h"
#include "runtime/SignalRgbRuntimeFactory.h"

namespace
{
bool Check(bool condition, const char* message)
{
    if(!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool TestPathUtils()
{
    using namespace signalbridge;
    return Check(NormalizeLookupPath("devices\\foo/../bar.js") == "devices/bar.js", "NormalizeLookupPath collapses separators and ..") &&
           Check(LookupDir("devices/keyboards/foo.js") == "devices/keyboards", "LookupDir returns parent lookup path") &&
           Check(JoinLookupPath("devices", "foo.js") == "devices/foo.js", "JoinLookupPath joins relative module paths");
}

bool TestControlParameters()
{
    using namespace signalbridge;

    QJsonObject checkbox;
    checkbox.insert("type", "checkbox");
    checkbox.insert("default", "true");
    QJsonObject number;
    number.insert("type", "number");
    number.insert("default", "3.5");
    number.insert("step", "0.25");
    QJsonObject select;
    select.insert("type", "select");
    select.insert("values", QJsonArray{ "RGB", "GRB" });
    select.insert("default", "GRB");

    return Check(NormalizeParameterValue(checkbox, QJsonValue()).toBool(false), "checkbox default normalizes truthy strings") &&
           Check(NormalizeParameterValue(number, QStringLiteral("4.25")).toDouble() == 4.25, "number string normalizes to number") &&
           Check(ParameterNumberDecimals(number, ParameterNumberStep(number)) == 2, "number decimals follow step precision") &&
           Check(NormalizeParameterValue(select, "BGR").toString() == "GRB", "select rejects values outside option list");
}

bool TestDeviceConfigStore()
{
    using namespace signalbridge;

    QTemporaryDir temp;
    if(!Check(temp.isValid(), "temporary config directory is valid"))
    {
        return false;
    }

    const filesystem::path configuration_root(temp.path().toStdString());
    DeviceConfigStore store;
    store.Load(configuration_root);

    ScriptMeta meta;
    meta.lookup_path = "keyboard.js";
    const bool script_changed = store.SetDeviceConfigurationValue("keyboard.js", "mode", "script");
    const bool duplicate_changed = store.SetDeviceConfigurationValue("keyboard.js", "mode", "script");
    const bool device_changed = store.SetDeviceConfigurationValue("keyboard.js|SERIAL", "mode", "device");
    store.SetDeviceConfigurationValue("keyboard.js|SERIAL", "brightness", 75);

    const QJsonObject merged = store.ConfigurationForDevice("keyboard.js|SERIAL", meta);
    bool ok = Check(script_changed, "new script config reports a change") &&
              Check(!duplicate_changed, "unchanged config does not rewrite") &&
              Check(device_changed, "new device config reports a change") &&
              Check(merged.value("mode").toString() == "device", "device config overrides script defaults") &&
              Check(merged.value("brightness").toInt() == 75, "device config preserves extra exact values");

    DeviceConfigStore reloaded;
    reloaded.Load(configuration_root);
    const QJsonObject saved = reloaded.ConfigurationForDevice("keyboard.js|SERIAL", meta);
    ok = ok &&
         Check(saved.value("mode").toString() == "device", "atomic saved config reloads as complete JSON") &&
         Check(saved.value("brightness").toInt() == 75, "atomic saved config preserves numeric values");

    QTemporaryDir legacy_temp;
    ok = Check(legacy_temp.isValid(), "temporary legacy config directory is valid") && ok;
    if(legacy_temp.isValid())
    {
        const QString legacy_dir = legacy_temp.path() + QStringLiteral("/SignalBridge");
        filesystem::create_directories(filesystem::path(legacy_dir.toStdString()));
        QFile legacy_file(legacy_dir + QStringLiteral("/device_config.json"));
        if(legacy_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            QJsonObject legacy_root;
            QJsonObject legacy_device;
            legacy_device.insert("mode", "legacy");
            legacy_root.insert("keyboard.js|SERIAL", legacy_device);
            legacy_file.write(QJsonDocument(legacy_root).toJson(QJsonDocument::Compact));
            legacy_file.close();
        }

        DeviceConfigStore legacy_store;
        legacy_store.Load(filesystem::path(legacy_temp.path().toStdString()));
        ok = Check(legacy_store.ConfigurationForDevice("keyboard.js|SERIAL", meta).value("mode").toString() == "legacy",
                   "legacy root config format still loads") &&
             ok;
    }

    return ok;
}

QJsonValue RuntimeGlobal(signalbridge::QuickJsRuntime& runtime, const QString& name)
{
    QJsonArray args;
    args.append(name);
    return runtime.CallGlobalJson("__test_get", args);
}

bool TestSystemInfoModule()
{
    using namespace signalbridge;

    const std::string lookup = "systeminfo-test.js";
    const std::string source = R"JS(
import systeminfo, { systeminfo as namedSysteminfo } from "@SignalRGB/systeminfo";
import suffixedSysteminfo from "@SignalRGB/systeminfo.js";
export function ReadSystemInfo() {
    return {
        sameExport: systeminfo === namedSysteminfo,
        sameSpecifier: systeminfo === suffixedSysteminfo,
        motherboard: systeminfo.GetMotherboardInfo(),
        bios: systeminfo.GetBiosInfo(),
    };
}
)JS";

    QuickJsRuntime runtime = SignalRgbRuntimeFactory::CreateScan();
    runtime.LoadModule(lookup, { ScriptSource{ lookup, lookup, source } });
    runtime.EvaluateModule();

    const QJsonObject info = runtime.CallModuleExportJson("ReadSystemInfo").toObject();
    const QJsonObject motherboard = info.value("motherboard").toObject();
    const QJsonObject bios = info.value("bios").toObject();

    const QByteArray motherboard_json = QJsonDocument(motherboard).toJson(QJsonDocument::Compact);
    const QByteArray bios_json = QJsonDocument(bios).toJson(QJsonDocument::Compact);
    std::cout << "SystemInfo motherboard: " << motherboard_json.constData() << '\n';
    std::cout << "SystemInfo bios: " << bios_json.constData() << '\n';

    return Check(info.value("sameExport").toBool(false), "systeminfo default and named exports share implementation") &&
           Check(info.value("sameSpecifier").toBool(false), "systeminfo import with .js suffix resolves to same module") &&
           Check(runtime.LoadedModuleSources().size() == 1, "resource modules are not recorded as user script sources") &&
           Check(motherboard.contains("model"), "motherboard info exposes model field") &&
           Check(motherboard.contains("manufacturer"), "motherboard info exposes manufacturer field") &&
           Check(motherboard.contains("product"), "motherboard info exposes product field") &&
           Check(motherboard.contains("vendor"), "motherboard info exposes vendor field") &&
           Check(bios.contains("releaseDate"), "bios info exposes SignalRGB releaseDate field");
}

bool TestSignalRgbBuiltinModules()
{
    using namespace signalbridge;

    const std::string lookup = "builtin-modules-test.js";
    const std::string source = R"JS(
import DeviceDiscovery from "@SignalRGB/DeviceDiscovery";
import errors, { Assert, ContextError, globalContext } from "@SignalRGB/Errors";
import LCD from "@SignalRGB/lcd";
import permissions from "@SignalRGB/permissions";
import serial, { serial as namedSerial } from "@SignalRGB/serial";

export function ProbeBuiltins() {
    globalContext.set("answer", 42);
    const contextError = ContextError("broken");
    return {
        discoveryGlobal: DeviceDiscovery === globalThis.DeviceDiscovery,
        discoveryCall: DeviceDiscovery.foundVirtualDevice() === undefined,
        sameAssert: errors.Assert === Assert,
        contextErrorName: contextError.name,
        contextErrorMessage: contextError.message,
        globalContextValue: globalContext.get("answer"),
        lcdGlobal: LCD === globalThis.LCD,
        lcdFrameEmpty: LCD.getFrame().length === 0,
        permissionsGlobal: permissions === globalThis.permissions,
        permissionsEmpty: permissions.permissions().length === 0,
        serialSameExport: serial === namedSerial,
        serialPortsEmpty: serial.availablePorts().length === 0,
        serialInfoEmpty: Object.keys(serial.getDeviceInfo()).length === 0,
        serialConnectFalse: serial.connect() === false,
        serialConnectedFalse: serial.isConnected() === false,
        serialWriteZero: serial.write([1]) === 0,
        serialReadEmpty: serial.read().length === 0,
        serialReadAllEmpty: serial.readAll().length === 0,
        serialPortNameEmpty: serial.getPortName() === "",
        serialBaudRateZero: serial.getBaudRate() === 0,
        serialGlobalAbsent: globalThis.serial === undefined,
    };
}
)JS";

    QuickJsRuntime runtime = SignalRgbRuntimeFactory::CreateScan();
    runtime.LoadModule(lookup, { ScriptSource{ lookup, lookup, source } });
    runtime.EvaluateModule();

    const QJsonObject info = runtime.CallModuleExportJson("ProbeBuiltins").toObject();
    return Check(info.value("discoveryGlobal").toBool(false), "DeviceDiscovery module reuses compatibility global") &&
           Check(info.value("discoveryCall").toBool(false), "DeviceDiscovery compatibility method is callable") &&
           Check(info.value("sameAssert").toBool(false), "Errors module default and named Assert share implementation") &&
           Check(info.value("contextErrorName").toString() == "ContextError", "ContextError exposes SignalRGB name") &&
           Check(info.value("contextErrorMessage").toString() == "broken", "ContextError preserves message") &&
           Check(info.value("globalContextValue").toInt() == 42, "globalContext stores values in runtime state") &&
           Check(info.value("lcdGlobal").toBool(false), "LCD module reuses compatibility global") &&
           Check(info.value("lcdFrameEmpty").toBool(false), "LCD compatibility frame is empty") &&
           Check(info.value("permissionsGlobal").toBool(false), "permissions module reuses compatibility global") &&
           Check(info.value("permissionsEmpty").toBool(false), "permissions compatibility list is empty") &&
           Check(info.value("serialSameExport").toBool(false), "serial default and named exports share implementation") &&
           Check(info.value("serialPortsEmpty").toBool(false), "serial availablePorts remains empty") &&
           Check(info.value("serialInfoEmpty").toBool(false), "serial getDeviceInfo remains empty") &&
           Check(info.value("serialConnectFalse").toBool(false), "serial connect remains unavailable") &&
           Check(info.value("serialConnectedFalse").toBool(false), "serial isConnected remains false") &&
           Check(info.value("serialWriteZero").toBool(false), "serial write remains unavailable") &&
           Check(info.value("serialReadEmpty").toBool(false), "serial read remains empty") &&
           Check(info.value("serialReadAllEmpty").toBool(false), "serial readAll remains empty") &&
           Check(info.value("serialPortNameEmpty").toBool(false), "serial getPortName remains empty") &&
           Check(info.value("serialBaudRateZero").toBool(false), "serial getBaudRate remains zero") &&
           Check(info.value("serialGlobalAbsent").toBool(false), "serial import does not create a global serial object") &&
           Check(runtime.LoadedModuleSources().size() == 1, "native builtin modules are not recorded as user script sources");
}

bool TestNativeScanRuntime()
{
    using namespace signalbridge;

    const std::string lookup = "native-scan-test.js";
    const std::string source = R"JS(
export function Probe() {
    return {
        write: device.write([0, 1, 2]),
        read: device.read(4, 0),
        hid: device.getHidInfo(),
        endpoints: device.getHidEndpoints(),
    };
}
)JS";

    QuickJsRuntime runtime = SignalRgbRuntimeFactory::CreateScan();
    runtime.LoadModule(lookup, { ScriptSource{ lookup, lookup, source } });
    runtime.EvaluateModule();

    const QJsonObject info = runtime.CallModuleExportJson("Probe").toObject();

    return Check(!runtime.HasGlobal("_hid_write"), "native runtime does not expose private _hid_write global") &&
           Check(!runtime.HasGlobal("__srgb_take_topology_update"), "native runtime does not expose private topology global") &&
           Check(info.value("write").toInt(-1) == 0, "scan runtime HID write safely returns zero") &&
           Check(info.value("read").toArray().isEmpty(), "scan runtime HID read safely returns empty array") &&
           Check(info.value("hid").toObject().value("vid").toInt(-1) == 0, "scan runtime exposes empty HID info") &&
           Check(info.value("endpoints").toArray().isEmpty(), "scan runtime exposes empty HID endpoints");
}

bool TestNativeDeviceRuntime()
{
    using namespace signalbridge;

    const std::string lookup = "native-device-test.js";
    const std::string source = R"JS(
export function Initialize() {
    globalThis.hidInfo = device.getHidInfo();
    globalThis.deviceInfo = device.getDeviceInfo();
    globalThis.endpoints = device.getHidEndpoints();
    device.setName("Native Device");
    device.setSize([2, 1]);
    device.setControllableLeds(["A", "B"], [[0, 0], [1, 0]]);
}
export function Render() {
    globalThis.firstColor = device.color(0, 0);
}
export function Snapshot() {
    return {
        hidInfo: globalThis.hidInfo,
        deviceInfo: globalThis.deviceInfo,
        endpoints: globalThis.endpoints,
        firstColor: globalThis.firstColor,
    };
}
)JS";

    ScriptMeta meta;
    meta.lookup_path = lookup;
    meta.source_path = lookup;
    meta.name = "Native Device Test";
    meta.module_sources = { ScriptSource{ lookup, lookup, source } };

    HidInfo hid;
    hid.vid = 0x1234;
    hid.pid = 0x5678;
    hid.product = "AK820";

    std::vector<EndpointDescriptor> endpoints;
    endpoints.push_back(EndpointDescriptor{ 2, 1, 0xFF00 });

    QuickJsRuntime runtime = SignalRgbRuntimeFactory::CreateDeviceRuntime(
        nullptr,
        meta,
        0,
        hid,
        {},
        endpoints);
    runtime.CallModuleExportJson("Initialize");

    const QJsonObject topology = runtime.TakeTopologyUpdate(true);
    RuntimeCallbackState* runtime_state = runtime.MutableState();
    runtime_state->device.main_frame.width = 2;
    runtime_state->device.main_frame.led_count = 2;
    runtime_state->device.main_frame.colors = { 7, 8, 9, 1, 2, 3 };
    runtime.CallModuleExportJson("Render");
    const QJsonObject snapshot = runtime.CallModuleExportJson("Snapshot").toObject();
    const QJsonArray color = snapshot.value("firstColor").toArray();

    return Check(topology.value("name").toString() == "Native Device", "native device runtime exports script-set name") &&
           Check(topology.value("main").toObject().value("led_count").toInt() == 2, "native device runtime exports script-set LEDs") &&
           Check(snapshot.value("hidInfo").toObject().value("vid").toInt() == 0x1234, "native device runtime exposes primary VID") &&
           Check(snapshot.value("hidInfo").toObject().value("pid").toInt() == 0x5678, "native device runtime exposes primary PID") &&
           Check(snapshot.value("deviceInfo").toObject().value("product").toString() == "AK820", "native device runtime exposes HID product name") &&
           Check(snapshot.value("endpoints").toArray().size() == 1, "native device runtime exposes HID endpoints") &&
           Check(color.size() == 3 && color.at(0).toInt() == 7 && color.at(1).toInt() == 8 && color.at(2).toInt() == 9,
                 "native device runtime reads OpenRGB frame through device.color");
}

bool TestSerialDeviceRuntime()
{
    using namespace signalbridge;

    const std::string lookup = "serial-device-test.js";
    const std::string source = R"JS(
import serial from "@SignalRGB/serial";
export function Probe() {
    const ports = serial.availablePorts();
    const info = serial.getDeviceInfo();
    const first = ports.length > 0 ? ports[0] : {};
    return {
        portCount: ports.length,
        firstPortName: first.portName,
        firstVendorId: first.vendorId,
        firstProductId: first.productId,
        infoPortName: info.portName,
        infoVendorId: info.vendorId,
        infoProductId: info.productId,
        currentPortName: serial.getPortName(),
        baudRate: serial.getBaudRate(),
        connected: serial.isConnected(),
    };
}
)JS";

    ScriptMeta meta;
    meta.lookup_path = lookup;
    meta.source_path = lookup;
    meta.name = "Serial Device Test";
    meta.module_sources = { ScriptSource{ lookup, lookup, source } };

    SerialInfo serial;
    serial.port_name = "COM_SIGNALBRIDGE_TEST";
    serial.system_location = "\\\\.\\COM_SIGNALBRIDGE_TEST";
    serial.serial_number = "SERIALTEST";
    serial.vid = 0x1A86;
    serial.pid = 0x7523;
    serial.has_vid = true;
    serial.has_pid = true;

    QuickJsRuntime runtime = SignalRgbRuntimeFactory::CreateDeviceRuntime(
        nullptr,
        meta,
        0,
        HidInfo(),
        {},
        {},
        QJsonObject(),
        {},
        std::make_shared<SerialBackend>(),
        serial);

    const QJsonObject info = runtime.CallModuleExportJson("Probe").toObject();
    return Check(info.value("portCount").toInt(0) >= 1, "serial runtime exposes primary serial port") &&
           Check(info.value("firstPortName").toString() == "COM_SIGNALBRIDGE_TEST", "primary serial port is listed first") &&
           Check(info.value("firstVendorId").toInt() == 0x1A86, "serial availablePorts exposes vendorId") &&
           Check(info.value("firstProductId").toInt() == 0x7523, "serial availablePorts exposes productId") &&
           Check(info.value("infoPortName").toString() == "COM_SIGNALBRIDGE_TEST", "serial getDeviceInfo exposes current port") &&
           Check(info.value("infoVendorId").toInt() == 0x1A86, "serial getDeviceInfo exposes vendorId") &&
           Check(info.value("infoProductId").toInt() == 0x7523, "serial getDeviceInfo exposes productId") &&
           Check(info.value("currentPortName").toString() == "COM_SIGNALBRIDGE_TEST", "serial getPortName uses active runtime port") &&
           Check(info.value("baudRate").toInt(-1) == 0, "serial getBaudRate is zero before connection") &&
           Check(!info.value("connected").toBool(true), "serial isConnected is false before connection");
}

bool TestRuntimeConfigurationCallbacks()
{
    using namespace signalbridge;

    const std::string lookup = "runtime-test.js";
    const std::string source = R"JS(
globalThis.modeCallbackCount = 0;
export function Name() { return "Runtime Test"; }
export function ControllableParameters() {
    return [
        { property: "mode", type: "combobox", values: ["Canvas", "Forced"], default: "Canvas" },
        { property: "channelCount", type: "number", min: "1", max: "2", default: "1" }
    ];
}
export function Initialize() {
    device.addChannel("Channel 1", 10);
}
export function onmodeChanged() {
    globalThis.modeCallbackCount = (globalThis.modeCallbackCount || 0) + 1;
    globalThis.modeCallbackValue = mode;
}
export function onchannelCountChanged() {
    device.removeChannel("Channel 1");
    device.removeChannel("Channel 2");
    if (channelCount <= 1) {
        device.addChannel("Channel 1", 10);
    } else {
        device.addChannel("Channel 2", 20);
    }
}
export function Render() {
    globalThis.renderMode = mode;
}
)JS";

    QuickJsRuntime runtime = SignalRgbRuntimeFactory::CreateScan();
    runtime.Eval("function __test_get(name) { return globalThis[name]; }", "<test-get>");
    runtime.LoadModule(lookup, { ScriptSource{ lookup, lookup, source } });
    runtime.EvaluateModule();

    ScriptMeta meta;
    meta.lookup_path = lookup;
    meta.source_path = lookup;
    meta.name = "Runtime Test";
    MergeControlParameters(meta.control_parameters, runtime.CallModuleExportJson("ControllableParameters"));
    runtime.ApplyConfiguration(meta, QJsonObject());

    bool ok = Check(RuntimeGlobal(runtime, "mode").toString() == "Canvas", "initial config applies default global value") &&
              Check(RuntimeGlobal(runtime, "modeCallbackCount").toInt() == 0, "initial config does not call onChanged callbacks");

    runtime.CallModuleExportJson("Initialize");
    const QJsonObject initial_topology = runtime.TakeTopologyUpdate(true);
    ok = Check(initial_topology.value("channels").toArray().first().toObject().value("name").toString() == "Channel 1",
               "initial topology contains first channel") &&
         ok;

    QJsonObject configuration;
    configuration.insert("mode", "Forced");
    runtime.ApplyConfigurationChange(meta, configuration, "mode");
    runtime.CallModuleExportJson("Render");
    ok = Check(RuntimeGlobal(runtime, "mode").toString() == "Forced", "runtime config change updates global value") &&
         Check(RuntimeGlobal(runtime, "modeCallbackCount").toInt() == 1, "runtime config change calls matching callback once") &&
         Check(RuntimeGlobal(runtime, "modeCallbackValue").toString() == "Forced", "callback sees normalized global value") &&
         Check(RuntimeGlobal(runtime, "renderMode").toString() == "Forced", "render observes updated config value") &&
         ok;

    configuration.insert("channelCount", 2);
    runtime.ApplyConfigurationChange(meta, configuration, "channelCount");
    const QJsonObject changed_topology = runtime.TakeTopologyUpdate(false);
    ok = Check(changed_topology.value("channels").toArray().first().toObject().value("name").toString() == "Channel 2",
               "config callback can dirty and replace runtime topology") &&
         ok;

    ok = Check(runtime.TakeTopologyUpdate(false).isEmpty(),
               "topology dirty flag is cleared after it is consumed") &&
         ok;

    return ok;
}

bool TestTopologyAndFrame()
{
    using namespace signalbridge;

    ScriptMeta meta;
    meta.name = "Test Keyboard";
    meta.width = 2;
    meta.height = 2;
    meta.led_names = { "Esc", "F1" };
    meta.led_positions = { { 0, 0 }, { 1, 0 } };

    TopologyResult topology = BuildOpenRgbTopology(QJsonObject(), meta, meta.name);
    std::vector<led> leds;
    std::vector<std::string> alt_names;
    RebuildOpenRgbLedList(topology.zones, topology.targets, DEVICE_TYPE_KEYBOARD, leds, alt_names);

    bool ok = Check(topology.zones.size() == 1, "fallback topology creates one zone") &&
              Check(topology.zones[0].type == ZONE_TYPE_MATRIX, "fallback topology uses matrix zone for 2D layout") &&
              Check(leds.size() == 2, "led list follows metadata led count") &&
              Check(leds[0].name == "Key: Esc", "keyboard led names get OpenRGB key prefix");

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
    ok = Check(topology.zones[0].matrix_map.width == 2 && topology.zones[0].matrix_map.height == 2,
               "API5 matrix map keeps its dimensions by value") &&
         Check(topology.zones[0].matrix_map.map.size() == 4 && topology.zones[0].matrix_map.map[0] == 0 &&
                   topology.zones[0].matrix_map.map[1] == 1 && topology.zones[0].matrix_map.map[2] == 0xFFFFFFFF,
               "API5 matrix map stores LED indices in its vector") &&
         ok;
#else
    ok = Check(topology.zones[0].matrix_map != nullptr && topology.zones[0].matrix_map->width == 2 &&
                   topology.zones[0].matrix_map->height == 2,
               "API4 matrix map keeps its allocated dimensions") &&
         Check(topology.zones[0].matrix_map != nullptr && topology.zones[0].matrix_map->map[0] == 0 &&
                   topology.zones[0].matrix_map->map[1] == 1 && topology.zones[0].matrix_map->map[2] == 0xFFFFFFFF,
               "API4 matrix map stores LED indices in its allocated array") &&
         ok;
#endif

    std::vector<RGBColor> colors = {
        ToRGBColor(1, 2, 3),
        ToRGBColor(4, 5, 6),
    };
    RuntimeColorFrame frame;
    BuildFrameForZone(topology.zones, colors, 0, topology.targets[0], frame);
    const std::vector<std::uint8_t>& bytes = frame.colors;
    ok = ok &&
         Check(bytes.size() == 12, "matrix frame expands to canvas cell count") &&
         Check(bytes.at(0) == 1 && bytes.at(1) == 2 && bytes.at(2) == 3, "frame encodes first RGB triplet") &&
         Check(bytes.at(3) == 4 && bytes.at(4) == 5 && bytes.at(5) == 6, "frame encodes second RGB triplet");

    DeleteZoneMaps(topology.zones);
    return ok;
}
}

int main()
{
    bool ok = true;
    ok = TestPathUtils() && ok;
    ok = TestControlParameters() && ok;
    ok = TestDeviceConfigStore() && ok;
    ok = TestSystemInfoModule() && ok;
    ok = TestSignalRgbBuiltinModules() && ok;
    ok = TestNativeScanRuntime() && ok;
    ok = TestNativeDeviceRuntime() && ok;
    ok = TestSerialDeviceRuntime() && ok;
    ok = TestRuntimeConfigurationCallbacks() && ok;
    ok = TestTopologyAndFrame() && ok;
    if(ok)
    {
        std::cout << "SignalBridgeCoreTests passed\n";
        return 0;
    }
    return 1;
}
