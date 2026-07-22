#ifndef SIGNALBRIDGE_CONTROLLER_H
#define SIGNALBRIDGE_CONTROLLER_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "domain/ScriptTypes.h"
#include "hid/HidBackend.h"
#include "openrgb/EndpointSession.h"
#include "openrgb/OpenRgbCompat.h"
#include "openrgb/TopologyMapper.h"
#include "runtime/QuickJsRuntime.h"
#include "serial/SerialBackend.h"

namespace signalbridge
{
class SignalBridgeController
#if SIGNALBRIDGE_OPENRGB_API_VERSION == 4
    : public RGBController
#endif
{
public:
    SignalBridgeController(
        std::shared_ptr<HidBackend> hid_backend,
        ScriptMeta meta,
        HidInfo primary_hid,
        QJsonObject configuration = QJsonObject(),
        std::string config_key = {},
        ScriptLogCallback log_callback = {});
    SignalBridgeController(
        std::shared_ptr<SerialBackend> serial_backend,
        ScriptMeta meta,
        SerialInfo primary_serial,
        QJsonObject configuration = QJsonObject(),
        std::string config_key = {},
        ScriptLogCallback log_callback = {});
    ~SignalBridgeController()
#if SIGNALBRIDGE_OPENRGB_API_VERSION == 4
        override
#endif
        ;

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 4
    void SetupZones() override;
    void ResizeZone(int zone, int new_size) override;

    void DeviceUpdateLEDs() override;
    void UpdateZoneLEDs(int zone) override;
    void UpdateSingleLED(int led) override;

    void SetCustomMode() override;
    void DeviceUpdateMode() override;
#else
    RGBController_Setup OpenRgbSetup() const;
    void AttachOpenRgbController(OpenRgbHostInterface* host, OpenRgbControllerInterface* controller);
    void DetachOpenRgbController();

    void DeviceUpdateLEDs();
    void DeviceUpdateZoneLEDs(int zone);
    void DeviceUpdateSingleLED(int led);
    void DeviceUpdateMode();
    void DeviceUpdateZoneMode(int zone);
    void DeviceSaveMode();
    void DeviceConfigureZone(int zone);
#endif

    const std::string& ConfigKey() const;
    const ScriptMeta& ScriptMetadata() const;
    std::string Name() const;
    void SetConfigurationValue(const QString& property, const QJsonValue& value);

private:
    void CreateRuntime();
    void InitializeScript();
    void DrainPendingConfigurationChanges();
    void LogConfigurationError(const QString& property, const std::string& message) const;
    void BuildZonesFromTopology(const QJsonObject& topology);
    void SnapshotOpenRgbState(std::vector<zone>& zones, std::vector<RGBColor>& colors) const;

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
    static void DeviceConfigureZoneCallback(void* context, int zone);
    static void DeviceUpdateLEDsCallback(void* context);
    static void DeviceUpdateZoneLEDsCallback(void* context, int zone);
    static void DeviceUpdateSingleLEDCallback(void* context, int led);
    static void DeviceUpdateModeCallback(void* context);
    static void DeviceUpdateZoneModeCallback(void* context, int zone);
    static void DeviceSaveModeCallback(void* context);
    void TopologyUpdateWorker();
#endif

    std::shared_ptr<HidBackend> hid_backend_;
    std::shared_ptr<SerialBackend> serial_backend_;
    ScriptMeta meta_;
    HidInfo primary_hid_;
    SerialInfo primary_serial_;
    QJsonObject configuration_;
    std::string config_key_;
    ScriptLogCallback log_callback_;
    std::unique_ptr<EndpointSession> endpoint_session_;
    std::unique_ptr<QuickJsRuntime> runtime_;
    std::vector<ZoneTarget> zone_targets_;
    std::vector<QString> pending_configuration_changes_;
    mutable std::mutex mutex_;
    bool shutting_down_ = false;

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
    RGBController_Setup setup_{};
    std::vector<std::string> led_display_names_;
    OpenRgbHostInterface* openrgb_host_ = nullptr;
    OpenRgbControllerInterface* openrgb_controller_ = nullptr;
    std::condition_variable topology_update_cv_;
    std::thread topology_update_thread_;
    std::size_t published_led_count_ = 0;
    bool topology_update_pending_ = false;
    bool stop_topology_update_thread_ = false;
#endif
};
}

#endif // SIGNALBRIDGE_CONTROLLER_H
