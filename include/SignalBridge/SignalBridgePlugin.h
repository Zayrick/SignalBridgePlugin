#ifndef SIGNALBRIDGEPLUGIN_H
#define SIGNALBRIDGEPLUGIN_H

#include <memory>

#include <QMenu>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "OpenRGBPluginInterface.h"
#include "SignalBridgePlugin_global.h"

#ifndef SIGNALBRIDGE_OPENRGB_API_VERSION
#define SIGNALBRIDGE_OPENRGB_API_VERSION 4
#endif

namespace signalbridge
{
class SignalBridgePluginCore;
}

class SIGNALBRIDGEPLUGIN_EXPORT SignalBridgePlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "SignalBridgePlugin.json")
#else
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID)
#endif
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    explicit SignalBridgePlugin(QObject* parent = nullptr);
    ~SignalBridgePlugin() override;

    OpenRGBPluginInfo GetPluginInfo() override;
    unsigned int GetPluginAPIVersion() override;

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
    void Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
#else
    void Load(ResourceManagerInterface* resource_manager_ptr) override;
#endif
    QWidget* GetWidget() override;
    QMenu* GetTrayMenu() override;
    void Unload() override;

#if SIGNALBRIDGE_OPENRGB_API_VERSION == 5
    void OnProfileAboutToLoad() override;
    void OnProfileLoad(nlohmann::json profile_data) override;
    nlohmann::json OnProfileSave() override;
    unsigned char* OnSDKCommand(unsigned int packet_id, unsigned char* packet_data, unsigned int* packet_size) override;
    void ProfileManagerUpdated(unsigned int update_reason) override;
    void ResourceManagerUpdated(unsigned int update_reason) override;
    void SettingsManagerUpdated(unsigned int update_reason) override;
#endif

signals:
    void DiscoveryStatusChanged(
        int generation,
        const QString& status,
        const QString& details,
        const QStringList& scripts,
        const QString& devices,
        bool running,
        int progress);
    void ScriptLogReceived(const QString& line);

private slots:
    void ApplyDiscoveryStatus(
        int generation,
        const QString& status,
        const QString& details,
        const QStringList& scripts,
        const QString& devices,
        bool running,
        int progress);
    void AppendLogLine(const QString& line);

private:
    friend class signalbridge::SignalBridgePluginCore;

    std::unique_ptr<signalbridge::SignalBridgePluginCore> core_;
};

#endif // SIGNALBRIDGEPLUGIN_H
