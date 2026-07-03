#ifndef SIGNALBRIDGE_CONTROLLER_H
#define SIGNALBRIDGE_CONTROLLER_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "RGBController/RGBController.h"
#include "domain/ScriptTypes.h"
#include "hid/HidBackend.h"
#include "openrgb/EndpointSession.h"
#include "openrgb/TopologyMapper.h"
#include "runtime/QuickJsRuntime.h"

namespace signalbridge
{
class SignalBridgeController : public RGBController
{
public:
    SignalBridgeController(
        std::shared_ptr<HidBackend> hid_backend,
        ScriptMeta meta,
        HidInfo primary_hid,
        QJsonObject configuration = QJsonObject(),
        std::string config_key = {},
        ScriptLogCallback log_callback = {});
    ~SignalBridgeController() override;

    void SetupZones() override;
    void ResizeZone(int zone, int new_size) override;

    void DeviceUpdateLEDs() override;
    void UpdateZoneLEDs(int zone) override;
    void UpdateSingleLED(int led) override;

    void SetCustomMode() override;
    void DeviceUpdateMode() override;

    const std::string& SourcePath() const;
    const std::string& ConfigKey() const;
    const ScriptMeta& ScriptMetadata() const;
    void SetConfiguration(QJsonObject configuration);
    void SetConfigurationValue(const QString& property, const QJsonValue& value);

private:
    void CreateRuntime();
    void InitializeScript();
    void DrainPendingConfigurationChanges();
    void LogConfigurationError(const QString& property, const std::string& message) const;
    void BuildZonesFromTopology(const QJsonObject& topology);

    std::shared_ptr<HidBackend> hid_backend_;
    ScriptMeta meta_;
    HidInfo primary_hid_;
    QJsonObject configuration_;
    std::string config_key_;
    ScriptLogCallback log_callback_;
    std::unique_ptr<EndpointSession> endpoint_session_;
    std::unique_ptr<QuickJsRuntime> runtime_;
    std::vector<ZoneTarget> zone_targets_;
    std::vector<QString> pending_configuration_changes_;
    mutable std::mutex mutex_;
    bool shutting_down_ = false;
};
}

#endif // SIGNALBRIDGE_CONTROLLER_H
