#ifndef SIGNALBRIDGE_CONTROLLER_REGISTRY_H
#define SIGNALBRIDGE_CONTROLLER_REGISTRY_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QJsonValue>
#include <QString>

#include "ResourceManagerInterface.h"
#include "openrgb/SignalBridgeController.h"

namespace signalbridge
{
class ControllerRegistry
{
public:
    SignalBridgeController* Register(
        ResourceManagerInterface* manager,
        std::unique_ptr<SignalBridgeController> controller);
    void AbandonOpenRgbOwnedControllers();
    void UnregisterAndDeleteControllers(ResourceManagerInterface* manager);
    void ApplyConfiguration(
        ResourceManagerInterface* manager,
        const QString& key,
        const QString& property,
        const QJsonValue& value);

private:
    struct Entry
    {
        SignalBridgeController* controller = nullptr;
        std::string config_key;
    };

    std::mutex mutex_;
    std::vector<Entry> controllers_;
};
}

#endif // SIGNALBRIDGE_CONTROLLER_REGISTRY_H
