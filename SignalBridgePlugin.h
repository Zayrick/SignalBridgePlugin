#ifndef SIGNALBRIDGEPLUGIN_H
#define SIGNALBRIDGEPLUGIN_H

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QMenu>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "OpenRGBPluginInterface.h"
#include "ResourceManagerInterface.h"
#include "SignalBridgePlugin_global.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class RGBController_SignalBridgeScript;
class SignalBridgeHidBackend;
struct SignalBridgeHidInfo;
struct SignalBridgeScriptMeta;

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
    void DiscoveryStatusChanged(int generation, const QString& status, const QString& details, const QStringList& scripts, bool running, int progress);

private slots:
    void ApplyDiscoveryStatus(int generation, const QString& status, const QString& details, const QStringList& scripts, bool running, int progress);
    void ShowLogView();
    void ShowScriptListView();

private:
    void EnsureWidget();
    void DiscoverSignalRgbDevices();
    void DiscoveryWorker(int generation, ResourceManagerInterface* manager);
    void StopDiscoveryThread();
    bool IsDiscoveryStale(int generation) const;
    void RemoveControllers(ResourceManagerInterface* manager);
    bool ValidateScriptEndpoint(const SignalBridgeScriptMeta& meta, const SignalBridgeHidInfo& hid) const;
    void SetStatusText(const std::string& text);
    void SetActiveView(int index);
    void SetScriptTable(const QStringList& scripts, bool running);

    ResourceManagerInterface* resource_manager = nullptr;
    QWidget* widget = nullptr;
    QLabel* status_label = nullptr;
    QProgressBar* progress_bar = nullptr;
    QStackedWidget* view_stack = nullptr;
    QPlainTextEdit* details_text = nullptr;
    QTableWidget* scripts_table = nullptr;
    QPushButton* rescan_button = nullptr;
    QPushButton* log_view_button = nullptr;
    QPushButton* script_list_view_button = nullptr;
    std::shared_ptr<SignalBridgeHidBackend> hid_backend;
    std::vector<RGBController_SignalBridgeScript*> controllers;
    std::thread discovery_thread;
    std::atomic<bool> discovery_running{ false };
    std::atomic<bool> discovery_cancel_requested{ false };
    std::atomic<int> discovery_generation{ 0 };
    int discovery_progress = 0;
    std::string status_message;
    std::string details_message;
    QStringList script_table_items;
};

#endif // SIGNALBRIDGEPLUGIN_H
