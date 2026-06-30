#ifndef SIGNALBRIDGEPLUGIN_H
#define SIGNALBRIDGEPLUGIN_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
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
class QTabWidget;
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
    void ShowLogView();
    void ShowScriptListView();
    void ShowDeviceListView();
    void OnDeviceSelectionChanged();
    void AppendLogLine(const QString& line);

private:
    void EnsureWidget();
    void DiscoverSignalRgbDevices();
    void DiscoveryWorker(int generation, ResourceManagerInterface* manager);
    void StopDiscoveryThread();
    bool IsDiscoveryStale(int generation) const;
    void RemoveControllers(ResourceManagerInterface* manager);
    bool ValidateScriptEndpoint(const SignalBridgeScriptMeta& meta, const SignalBridgeHidInfo& hid, int generation);
    void SetStatusText(const std::string& text);
    void ClearLogOutput();
    void SetActiveView(int index);
    void SetScriptTable(const QStringList& scripts, bool running);
    void SetDeviceList(const QString& devices, bool running);
    QWidget* CreateDeviceConfigPage(const QJsonObject& device);
    void LoadDeviceConfigStore();
    void SaveDeviceConfigStore() const;
    QJsonObject ConfigurationForDevice(const QString& key, const QString& script_key) const;
    QJsonObject ConfigurationForDevice(const QString& key, const SignalBridgeScriptMeta& meta) const;
    void SetDeviceConfigurationValue(const QString& key, const QString& property, const QJsonValue& value);
    void ApplyConfigurationToControllers(const QString& key, const QString& property, const QJsonValue& value);

    ResourceManagerInterface* resource_manager = nullptr;
    QWidget* widget = nullptr;
    QLabel* status_label = nullptr;
    QProgressBar* progress_bar = nullptr;
    QStackedWidget* view_stack = nullptr;
    QPlainTextEdit* details_text = nullptr;
    QTableWidget* scripts_table = nullptr;
    QTabWidget* devices_tab_bar = nullptr;
    QPushButton* rescan_button = nullptr;
    QPushButton* log_view_button = nullptr;
    QPushButton* script_list_view_button = nullptr;
    QPushButton* device_list_view_button = nullptr;
    std::shared_ptr<SignalBridgeHidBackend> hid_backend;
    std::vector<RGBController_SignalBridgeScript*> controllers;
    mutable std::mutex controller_mutex;
    mutable std::mutex config_mutex;
    std::thread discovery_thread;
    std::atomic<bool> discovery_running{ false };
    std::atomic<bool> discovery_cancel_requested{ false };
    std::atomic<int> discovery_generation{ 0 };
    int discovery_progress = 0;
    std::string status_message;
    std::string details_message;
    QStringList log_lines;
    QStringList script_table_items;
    QJsonArray device_records;
    QJsonObject device_config_store;
    QString selected_device_key;
};

#endif // SIGNALBRIDGEPLUGIN_H
