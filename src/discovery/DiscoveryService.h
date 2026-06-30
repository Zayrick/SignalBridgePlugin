#ifndef SIGNALBRIDGE_DISCOVERY_SERVICE_H
#define SIGNALBRIDGE_DISCOVERY_SERVICE_H

#include <functional>

#include <QString>
#include <QStringList>

#include "ResourceManagerInterface.h"
#include "config/DeviceConfigStore.h"
#include "discovery/ControllerRegistry.h"

namespace signalbridge
{
struct DiscoveryCallbacks
{
    std::function<bool(int generation)> is_stale;
    std::function<void(
        int generation,
        const QString& status,
        const QString& details,
        const QStringList& scripts,
        const QString& devices,
        bool running,
        int progress)> status_changed;
    std::function<void(const QString& line)> log_line;
};

class DiscoveryService
{
public:
    void Discover(
        int generation,
        ResourceManagerInterface* manager,
        ControllerRegistry& registry,
        DeviceConfigStore& config_store,
        const DiscoveryCallbacks& callbacks) const;
};
}

#endif // SIGNALBRIDGE_DISCOVERY_SERVICE_H
