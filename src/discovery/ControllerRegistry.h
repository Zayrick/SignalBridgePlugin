#ifndef SIGNALBRIDGE_CONTROLLER_REGISTRY_H
#define SIGNALBRIDGE_CONTROLLER_REGISTRY_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QJsonValue>
#include <QString>

#include "openrgb/OpenRgbCompat.h"
#include "openrgb/SignalBridgeController.h"

namespace signalbridge
{
class ControllerRegistry
{
public:
    SignalBridgeController* Register(
        OpenRgbHostInterface* manager,
        std::unique_ptr<SignalBridgeController> controller);
    void AbandonOpenRgbOwnedControllers();
    void UnregisterAndDeleteControllers(OpenRgbHostInterface* manager);
    void ApplyConfiguration(
        OpenRgbHostInterface* manager,
        const QString& key,
        const QString& property,
        const QJsonValue& value);

private:
    struct Entry
    {
        SignalBridgeController* controller = nullptr;
        OpenRgbControllerInterface* openrgb_controller = nullptr;
        std::string config_key;
#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
        std::unique_ptr<SignalBridgeController> owned_controller;
#endif
    };

    std::mutex mutex_;
    std::vector<Entry> controllers_;
};
}

#endif // SIGNALBRIDGE_CONTROLLER_REGISTRY_H
