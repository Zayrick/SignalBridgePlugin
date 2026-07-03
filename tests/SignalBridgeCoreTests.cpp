#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ResourceManagerInterface.h"
#include "config/DeviceConfigStore.h"
#include "domain/ControlParameters.h"
#include "domain/PathUtils.h"
#include "domain/ScriptTypes.h"
#include "openrgb/FrameBuilder.h"
#include "openrgb/TopologyMapper.h"
#include "runtime/SignalRgbRuntimeFactory.h"

namespace
{
class FakeResourceManager : public ResourceManagerInterface
{
public:
    explicit FakeResourceManager(filesystem::path config_dir)
        : config_dir_(std::move(config_dir))
    {
    }

    std::vector<i2c_smbus_interface*>& GetI2CBusses() override { return i2c_busses_; }
    void RegisterRGBController(RGBController* rgb_controller) override { controllers_.push_back(rgb_controller); }
    void UnregisterRGBController(RGBController* rgb_controller) override
    {
        controllers_.erase(std::remove(controllers_.begin(), controllers_.end(), rgb_controller), controllers_.end());
    }
    void RegisterDeviceListChangeCallback(DeviceListChangeCallback, void*) override {}
    void RegisterDetectionProgressCallback(DetectionProgressCallback, void*) override {}
    void RegisterDetectionStartCallback(DetectionStartCallback, void*) override {}
    void RegisterDetectionEndCallback(DetectionEndCallback, void*) override {}
    void RegisterI2CBusListChangeCallback(I2CBusListChangeCallback, void*) override {}
    void UnregisterDeviceListChangeCallback(DeviceListChangeCallback, void*) override {}
    void UnregisterDetectionProgressCallback(DetectionProgressCallback, void*) override {}
    void UnregisterDetectionStartCallback(DetectionStartCallback, void*) override {}
    void UnregisterDetectionEndCallback(DetectionEndCallback, void*) override {}
    void UnregisterI2CBusListChangeCallback(I2CBusListChangeCallback, void*) override {}
    std::vector<RGBController*>& GetRGBControllers() override { return controllers_; }
    unsigned int GetDetectionPercent() override { return 0; }
    filesystem::path GetConfigurationDirectory() override { return config_dir_; }
    std::vector<NetworkClient*>& GetClients() override { return clients_; }
    NetworkServer* GetServer() override { return nullptr; }
    ProfileManager* GetProfileManager() override { return nullptr; }
    SettingsManager* GetSettingsManager() override { return nullptr; }
    void UpdateDeviceList() override {}
    void WaitForDeviceDetection() override {}

private:
    filesystem::path config_dir_;
    std::vector<i2c_smbus_interface*> i2c_busses_;
    std::vector<RGBController*> controllers_;
    std::vector<NetworkClient*> clients_;
};

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

    FakeResourceManager manager(filesystem::path(temp.path().toStdString()));
    DeviceConfigStore store;
    store.Load(&manager);

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
    reloaded.Load(&manager);
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

        FakeResourceManager legacy_manager(filesystem::path(legacy_temp.path().toStdString()));
        DeviceConfigStore legacy_store;
        legacy_store.Load(&legacy_manager);
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
export function ReadSystemInfo() {
    return {
        sameExport: systeminfo === namedSysteminfo,
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
           Check(motherboard.contains("model"), "motherboard info exposes model field") &&
           Check(motherboard.contains("manufacturer"), "motherboard info exposes manufacturer field") &&
           Check(motherboard.contains("product"), "motherboard info exposes product field") &&
           Check(motherboard.contains("vendor"), "motherboard info exposes vendor field") &&
           Check(bios.contains("releaseDate"), "bios info exposes SignalRGB releaseDate field");
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
    QJsonArray force_topology;
    force_topology.append(true);
    const QJsonObject initial_topology = runtime.CallGlobalJson("__srgb_take_topology_update", force_topology).toObject();
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
    QJsonArray dirty_topology;
    dirty_topology.append(false);
    const QJsonObject changed_topology = runtime.CallGlobalJson("__srgb_take_topology_update", dirty_topology).toObject();
    ok = Check(changed_topology.value("channels").toArray().first().toObject().value("name").toString() == "Channel 2",
               "config callback can dirty and replace runtime topology") &&
         ok;

    QJsonArray no_dirty_topology;
    no_dirty_topology.append(false);
    ok = Check(!runtime.CallGlobalJson("__srgb_take_topology_update", no_dirty_topology).toBool(true),
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

    std::vector<RGBColor> colors = {
        ToRGBColor(1, 2, 3),
        ToRGBColor(4, 5, 6),
    };
    const QJsonObject frame = BuildFrameForZone(topology.zones, colors, 0, topology.targets[0]);
    const QJsonArray bytes = frame.value("colors").toArray();
    ok = ok &&
         Check(bytes.size() == 12, "matrix frame expands to canvas cell count") &&
         Check(bytes.at(0).toInt() == 1 && bytes.at(1).toInt() == 2 && bytes.at(2).toInt() == 3, "frame encodes first RGB triplet") &&
         Check(bytes.at(3).toInt() == 4 && bytes.at(4).toInt() == 5 && bytes.at(5).toInt() == 6, "frame encodes second RGB triplet");

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
    ok = TestRuntimeConfigurationCallbacks() && ok;
    ok = TestTopologyAndFrame() && ok;
    if(ok)
    {
        std::cout << "SignalBridgeCoreTests passed\n";
        return 0;
    }
    return 1;
}
