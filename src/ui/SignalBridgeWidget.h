#ifndef SIGNALBRIDGE_WIDGET_H
#define SIGNALBRIDGE_WIDGET_H

#include <functional>

#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "ui/DeviceConfigPageFactory.h"
#include "ui/LogBuffer.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTabWidget;

namespace signalbridge
{
class SignalBridgeWidget : public QWidget
{
    Q_OBJECT

public:
    using RescanCallback = std::function<void()>;

    SignalBridgeWidget(
        DeviceConfigurationResolver configuration_resolver,
        DeviceConfigurationChangedCallback configuration_changed,
        RescanCallback rescan,
        QWidget* parent = nullptr);

    void SetResourceAvailable(bool available);
    void ClearLogOutput();
    void AppendLogLine(const QString& line);
    void ApplyDiscoveryStatus(
        const QString& status,
        const QString& details,
        const QStringList& scripts,
        const QString& devices,
        bool running,
        int progress);

private:
    void SetStatusText(const QString& text);
    void SetActiveView(int index);
    void SetScriptTable(const QStringList& scripts, bool running);
    void SetDeviceList(const QString& devices, bool running);
    void OnDeviceSelectionChanged();

    DeviceConfigurationResolver configuration_resolver_;
    DeviceConfigurationChangedCallback configuration_changed_;
    RescanCallback rescan_;
    QLabel* status_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QStackedWidget* view_stack_ = nullptr;
    QPlainTextEdit* details_text_ = nullptr;
    QTableWidget* scripts_table_ = nullptr;
    QTabWidget* devices_tab_bar_ = nullptr;
    QPushButton* rescan_button_ = nullptr;
    QPushButton* log_view_button_ = nullptr;
    QPushButton* script_list_view_button_ = nullptr;
    QPushButton* device_list_view_button_ = nullptr;
    bool resource_available_ = false;
    int discovery_progress_ = 0;
    QString status_message_;
    QStringList script_table_items_;
    QJsonArray device_records_;
    QString selected_device_key_;
    LogBuffer log_buffer_;
};
}

#endif // SIGNALBRIDGE_WIDGET_H
