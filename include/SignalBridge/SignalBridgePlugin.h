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

namespace signalbridge
{
class SignalBridgePluginCore;
}

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

    void EmitDiscoveryStatus(
        int generation,
        const QString& status,
        const QString& details,
        const QStringList& scripts,
        const QString& devices,
        bool running,
        int progress);
    void EmitScriptLog(const QString& line);

    std::unique_ptr<signalbridge::SignalBridgePluginCore> core_;
};

#endif // SIGNALBRIDGEPLUGIN_H
