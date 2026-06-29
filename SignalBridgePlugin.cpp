#include "SignalBridgePlugin.h"

SignalBridgePlugin::SignalBridgePlugin(QObject* parent)
    : QObject(parent)
{
}

SignalBridgePlugin::~SignalBridgePlugin() = default;

OpenRGBPluginInfo SignalBridgePlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;

    info.Name = "SignalBridge Plugin";
    info.Description = "Bridges OpenRGB signals to external integrations.";
    info.Version = SIGNALBRIDGEPLUGIN_VERSION;
    info.Commit = SIGNALBRIDGEPLUGIN_COMMIT;
    info.URL = "";
    info.Location = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label = "SignalBridge";

    return info;
}

unsigned int SignalBridgePlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void SignalBridgePlugin::Load(ResourceManagerInterface* resource_manager_ptr)
{
    resource_manager = resource_manager_ptr;
}

QWidget* SignalBridgePlugin::GetWidget()
{
    if(widget == nullptr)
    {
        widget = new QWidget();
        widget->setObjectName("SignalBridgePluginWidget");
    }

    return widget;
}

QMenu* SignalBridgePlugin::GetTrayMenu()
{
    return nullptr;
}

void SignalBridgePlugin::Unload()
{
    resource_manager = nullptr;
    widget = nullptr;
}
