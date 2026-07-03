#include "discovery/ControllerRegistry.h"

#include <algorithm>

namespace signalbridge
{
namespace
{
bool ContainsController(const std::vector<RGBController*>& controllers, RGBController* controller)
{
    return std::find(controllers.begin(), controllers.end(), controller) != controllers.end();
}
}

SignalBridgeController* ControllerRegistry::Register(
    ResourceManagerInterface* manager,
    std::unique_ptr<SignalBridgeController> controller)
{
    if(manager == nullptr || controller == nullptr)
    {
        return nullptr;
    }

    SignalBridgeController* raw = controller.get();
    const std::string config_key = raw->ConfigKey();
    manager->RegisterRGBController(raw);
    controller.release();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        controllers_.push_back({ raw, config_key });
    }
    return raw;
}

void ControllerRegistry::AbandonOpenRgbOwnedControllers()
{
    std::lock_guard<std::mutex> lock(mutex_);
    controllers_.clear();
}

void ControllerRegistry::UnregisterAndDeleteControllers(ResourceManagerInterface* manager)
{
    std::vector<Entry> controllers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        controllers.swap(controllers_);
    }

    if(manager == nullptr)
    {
        return;
    }

    for(const Entry& entry : controllers)
    {
        if(entry.controller == nullptr)
        {
            continue;
        }

        if(!ContainsController(manager->GetRGBControllers(), entry.controller))
        {
            continue;
        }

        manager->UnregisterRGBController(entry.controller);
        delete entry.controller;
    }
}

void ControllerRegistry::ApplyConfiguration(
    ResourceManagerInterface* manager,
    const QString& key,
    const QString& property,
    const QJsonValue& value)
{
    if(manager == nullptr)
    {
        return;
    }

    std::vector<SignalBridgeController*> matching;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::vector<RGBController*> live_controllers = manager->GetRGBControllers();
        controllers_.erase(
            std::remove_if(
                controllers_.begin(),
                controllers_.end(),
                [&live_controllers](const Entry& entry) {
                    return entry.controller == nullptr || !ContainsController(live_controllers, entry.controller);
                }),
            controllers_.end());

        for(const Entry& entry : controllers_)
        {
            if(QString::fromStdString(entry.config_key) != key)
            {
                continue;
            }
            matching.push_back(entry.controller);
        }
    }

    for(SignalBridgeController* controller : matching)
    {
        if(controller == nullptr)
        {
            continue;
        }
        if(ContainsController(manager->GetRGBControllers(), controller))
        {
            controller->SetConfigurationValue(property, value);
        }
    }
}
}
