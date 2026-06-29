#ifndef SIGNALBRIDGEPLUGIN_H
#define SIGNALBRIDGEPLUGIN_H

#include <QMenu>
#include <QObject>
#include <QWidget>

#include "OpenRGBPluginInterface.h"
#include "ResourceManagerInterface.h"
#include "SignalBridgePlugin_global.h"

class SIGNALBRIDGEPLUGIN_EXPORT SignalBridgePlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID)
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    explicit SignalBridgePlugin(QObject* parent = nullptr);
    ~SignalBridgePlugin() override;

    OpenRGBPluginInfo GetPluginInfo() override;
    unsigned int GetPluginAPIVersion() override;

    void Load(ResourceManagerInterface* resource_manager_ptr) override;
    QWidget* GetWidget() override;
    QMenu* GetTrayMenu() override;
    void Unload() override;

private:
    ResourceManagerInterface* resource_manager = nullptr;
    QWidget* widget = nullptr;
};

#endif // SIGNALBRIDGEPLUGIN_H
