#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ResourceManagerInterface.h"
#include "config/DeviceConfigStore.h"
#include "domain/ControlParameters.h"
#include "domain/PathUtils.h"
#include "domain/ScriptTypes.h"
#include "openrgb/FrameBuilder.h"
#include "openrgb/TopologyMapper.h"

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
    store.SetDeviceConfigurationValue("keyboard.js", "mode", "script");
    store.SetDeviceConfigurationValue("keyboard.js|SERIAL", "mode", "device");
    store.SetDeviceConfigurationValue("keyboard.js|SERIAL", "brightness", 75);

    const QJsonObject merged = store.ConfigurationForDevice("keyboard.js|SERIAL", meta);
    return Check(merged.value("mode").toString() == "device", "device config overrides script defaults") &&
           Check(merged.value("brightness").toInt() == 75, "device config preserves extra exact values");
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
    ok = TestTopologyAndFrame() && ok;
    if(ok)
    {
        std::cout << "SignalBridgeCoreTests passed\n";
        return 0;
    }
    return 1;
}
