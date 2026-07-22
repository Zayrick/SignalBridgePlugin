#include "discovery/ControllerRegistry.h"

#include <algorithm>

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
#include <atomic>
#include <thread>

#include <QCoreApplication>
#include <QEvent>
#include <QThread>
#endif

namespace signalbridge
{
namespace
{
bool ContainsController(
    const std::vector<OpenRgbControllerInterface*>& controllers,
    OpenRgbControllerInterface* controller)
{
    return std::find(controllers.begin(), controllers.end(), controller) != controllers.end();
}
}

SignalBridgeController* ControllerRegistry::Register(
    OpenRgbHostInterface* manager,
    std::unique_ptr<SignalBridgeController> controller)
{
    if(manager == nullptr || controller == nullptr)
    {
        return nullptr;
    }

    SignalBridgeController* raw = controller.get();
    const std::string config_key = raw->ConfigKey();

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
    RGBController_Setup setup = raw->OpenRgbSetup();
    OpenRgbControllerInterface* openrgb_controller = manager->CreateVirtualRGBController(&setup);
    if(openrgb_controller == nullptr)
    {
        return nullptr;
    }

    try
    {
        raw->AttachOpenRgbController(manager, openrgb_controller);
        manager->RegisterVirtualRGBController(openrgb_controller);
    }
    catch(...)
    {
        raw->DetachOpenRgbController();
        manager->UnregisterVirtualRGBController(openrgb_controller);
        manager->DeleteVirtualRGBController(openrgb_controller);
        throw;
    }
    if(!ContainsController(OpenRgbControllers(manager), openrgb_controller))
    {
        raw->DetachOpenRgbController();
        manager->UnregisterVirtualRGBController(openrgb_controller);
        manager->DeleteVirtualRGBController(openrgb_controller);
        return nullptr;
    }

    Entry entry;
    entry.controller = raw;
    entry.openrgb_controller = openrgb_controller;
    entry.config_key = config_key;
    entry.owned_controller = std::move(controller);
#else
    manager->RegisterRGBController(raw);
    controller.release();

    Entry entry;
    entry.controller = raw;
    entry.openrgb_controller = raw;
    entry.config_key = config_key;
#endif

    {
        std::lock_guard<std::mutex> lock(mutex_);
        controllers_.push_back(std::move(entry));
    }
    return raw;
}

void ControllerRegistry::AbandonOpenRgbOwnedControllers()
{
#if SIGNALBRIDGE_OPENRGB_API_VERSION == 4
    std::lock_guard<std::mutex> lock(mutex_);
    controllers_.clear();
#endif
}

void ControllerRegistry::UnregisterAndDeleteControllers(OpenRgbHostInterface* manager)
{
#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
    if(manager == nullptr)
    {
        return;
    }
#endif

    std::vector<Entry> controllers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        controllers.swap(controllers_);
    }

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
    auto cleanup = [manager](std::vector<Entry>& entries) {
        for(Entry& entry : entries)
        {
            if(entry.controller == nullptr)
            {
                continue;
            }

            entry.controller->DetachOpenRgbController();
            if(entry.openrgb_controller != nullptr)
            {
                manager->UnregisterVirtualRGBController(entry.openrgb_controller);
                manager->DeleteVirtualRGBController(entry.openrgb_controller);
                entry.openrgb_controller = nullptr;
            }
            entry.owned_controller.reset();
            entry.controller = nullptr;
        }
    };

    QCoreApplication* application = QCoreApplication::instance();
    const bool on_application_thread =
        application != nullptr && QThread::currentThread() == application->thread();
    if(on_application_thread)
    {
        std::atomic<bool> cleanup_finished{ false };
        std::thread cleanup_thread([&]() {
            try
            {
                cleanup(controllers);
            }
            catch(...)
            {
            }
            cleanup_finished.store(true, std::memory_order_release);
        });

        while(!cleanup_finished.load(std::memory_order_acquire))
        {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            std::this_thread::yield();
        }
        cleanup_thread.join();
    }
    else
    {
        cleanup(controllers);
    }
#else
    for(const Entry& entry : controllers)
    {
        if(entry.controller == nullptr)
        {
            continue;
        }

        if(manager == nullptr || !ContainsController(OpenRgbControllers(manager), entry.openrgb_controller))
        {
            continue;
        }

        manager->UnregisterRGBController(entry.controller);
        delete entry.controller;
    }
#endif
}

void ControllerRegistry::ApplyConfiguration(
    OpenRgbHostInterface* manager,
    const QString& key,
    const QString& property,
    const QJsonValue& value)
{
    if(manager == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
#if SIGNALBRIDGE_OPENRGB_API_VERSION == 4
    const std::vector<OpenRgbControllerInterface*> live_controllers = OpenRgbControllers(manager);
    controllers_.erase(
        std::remove_if(
            controllers_.begin(),
            controllers_.end(),
            [&live_controllers](const Entry& entry) {
                return entry.controller == nullptr || !ContainsController(live_controllers, entry.openrgb_controller);
            }),
        controllers_.end());
#endif

    for(const Entry& entry : controllers_)
    {
        if(entry.controller == nullptr || QString::fromStdString(entry.config_key) != key)
        {
            continue;
        }

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
        entry.controller->SetConfigurationValue(property, value);
#else
        if(ContainsController(live_controllers, entry.openrgb_controller))
        {
            entry.controller->SetConfigurationValue(property, value);
        }
#endif
    }
}
}
