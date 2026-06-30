#ifndef RGBCONTROLLER_SIGNALBRIDGESCRIPT_H
#define RGBCONTROLLER_SIGNALBRIDGESCRIPT_H

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QString>

#include "RGBController/RGBController.h"
#include "SignalBridgeScriptRuntime.h"

class RGBController_SignalBridgeScript : public RGBController
{
public:
    RGBController_SignalBridgeScript(
        std::shared_ptr<SignalBridgeHidBackend> hid_backend,
        SignalBridgeScriptMeta meta,
        SignalBridgeHidInfo primary_hid,
        QJsonObject configuration = QJsonObject(),
        std::string config_key = {});
    ~RGBController_SignalBridgeScript() override;

    void SetupZones() override;
    void ResizeZone(int zone, int new_size) override;

    void DeviceUpdateLEDs() override;
    void UpdateZoneLEDs(int zone) override;
    void UpdateSingleLED(int led) override;

    void SetCustomMode() override;
    void DeviceUpdateMode() override;

    const std::string& SourcePath() const;
    const std::string& ConfigKey() const;
    const SignalBridgeScriptMeta& ScriptMeta() const;
    void SetConfiguration(QJsonObject configuration);
    void SetConfigurationValue(const QString& property, const QJsonValue& value);

private:
    struct ZoneTarget
    {
        enum class Kind
        {
            Main,
            Channel,
            Subdevice,
        };

        Kind kind = Kind::Main;
        std::string key;
        unsigned int width = 1;
        unsigned int height = 1;
        std::vector<int> matrix_map;
    };

    void OpenEndpoints();
    void CreateRuntime();
    void InitializeScript();
    void BuildZonesFromTopology(const QJsonObject& topology);
    void RebuildLedList();
    void DeleteZoneMaps();
    void CloseHandles();

    QJsonObject BuildFrameForZone(unsigned int zone_index, const ZoneTarget& target) const;
    std::vector<unsigned char> BuildFlatColors(unsigned int start, unsigned int count) const;
    std::vector<unsigned char> BuildSpatialColors(unsigned int start, const ZoneTarget& target) const;

    static device_type ResolveDeviceType(const std::string& signalrgb_type);

    std::shared_ptr<SignalBridgeHidBackend> hid_backend_;
    SignalBridgeScriptMeta meta_;
    SignalBridgeHidInfo primary_hid_;
    QJsonObject configuration_;
    std::string config_key_;
    SignalBridgeHidBackend::Handle primary_handle_ = 0;
    std::map<std::string, SignalBridgeHidBackend::Handle> endpoint_handles_;
    std::vector<SignalBridgeEndpointDescriptor> endpoints_;
    std::unique_ptr<SignalBridgeJsRuntime> runtime_;
    std::vector<ZoneTarget> zone_targets_;
    mutable std::mutex mutex_;
    bool shutting_down_ = false;
};

#endif // RGBCONTROLLER_SIGNALBRIDGESCRIPT_H
