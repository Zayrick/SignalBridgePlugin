#include "discovery/ControllerRegistry.h"

namespace signalbridge
{
SignalBridgeController* ControllerRegistry::Register(
    ResourceManagerInterface* manager,
    std::unique_ptr<SignalBridgeController> controller)
{
    SignalBridgeController* raw = controller.get();
    manager->RegisterRGBController(raw);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        controllers_.push_back(std::move(controller));
    }
    return raw;
}

void ControllerRegistry::Clear(ResourceManagerInterface* manager)
{
    std::vector<std::unique_ptr<SignalBridgeController>> controllers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        controllers.swap(controllers_);
    }

    if(manager != nullptr)
    {
        for(const std::unique_ptr<SignalBridgeController>& controller : controllers)
        {
            manager->UnregisterRGBController(controller.get());
        }
    }
}

void ControllerRegistry::ApplyConfiguration(const QString& key, const QString& property, const QJsonValue& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(const std::unique_ptr<SignalBridgeController>& controller : controllers_)
    {
        if(controller == nullptr || QString::fromStdString(controller->ConfigKey()) != key)
        {
            continue;
        }
        controller->SetConfigurationValue(property, value);
    }
}
}
