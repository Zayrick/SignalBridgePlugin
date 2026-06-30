#ifndef SIGNALBRIDGE_CONTROLLER_REGISTRY_H
#define SIGNALBRIDGE_CONTROLLER_REGISTRY_H

#include <memory>
#include <mutex>
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
    void Clear(ResourceManagerInterface* manager);
    void ApplyConfiguration(const QString& key, const QString& property, const QJsonValue& value);

private:
    std::mutex mutex_;
    std::vector<std::unique_ptr<SignalBridgeController>> controllers_;
};
}

#endif // SIGNALBRIDGE_CONTROLLER_REGISTRY_H
