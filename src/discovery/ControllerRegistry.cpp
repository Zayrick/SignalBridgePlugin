#include "discovery/ControllerRegistry.h"

namespace signalbridge
{
SignalBridgeController* ControllerRegistry::Register(
    ResourceManagerInterface* manager,
    std::unique_ptr<SignalBridgeController> controller)
{
    std::shared_ptr<SignalBridgeController> owned(std::move(controller));
    SignalBridgeController* raw = owned.get();
    manager->RegisterRGBController(raw);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        controllers_.push_back(std::move(owned));
    }
    return raw;
}

void ControllerRegistry::Clear(ResourceManagerInterface* manager)
{
    std::vector<std::shared_ptr<SignalBridgeController>> controllers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        controllers.swap(controllers_);
    }

    if(manager != nullptr)
    {
        for(const std::shared_ptr<SignalBridgeController>& controller : controllers)
        {
            manager->UnregisterRGBController(controller.get());
        }
    }
}

void ControllerRegistry::ApplyConfiguration(const QString& key, const QString& property, const QJsonValue& value)
{
    std::vector<std::shared_ptr<SignalBridgeController>> matching;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const std::shared_ptr<SignalBridgeController>& controller : controllers_)
        {
            if(controller == nullptr || QString::fromStdString(controller->ConfigKey()) != key)
            {
                continue;
            }
            matching.push_back(controller);
        }
    }

    for(const std::shared_ptr<SignalBridgeController>& controller : matching)
    {
        if(controller == nullptr)
        {
            continue;
        }
        controller->SetConfigurationValue(property, value);
    }
}
}
