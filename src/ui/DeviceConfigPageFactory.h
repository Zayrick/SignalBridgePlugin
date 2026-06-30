#ifndef SIGNALBRIDGE_DEVICE_CONFIG_PAGE_FACTORY_H
#define SIGNALBRIDGE_DEVICE_CONFIG_PAGE_FACTORY_H

#include <functional>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class QTabWidget;
class QWidget;

namespace signalbridge
{
using DeviceConfigurationResolver = std::function<QJsonObject(const QString& key, const QString& script_key)>;
using DeviceConfigurationChangedCallback = std::function<void(const QString& key, const QString& property, const QJsonValue& value)>;

QWidget* CreateDeviceConfigPage(
    QTabWidget* parent,
    const QJsonObject& device,
    const DeviceConfigurationResolver& configuration_resolver,
    const DeviceConfigurationChangedCallback& configuration_changed);
}

#endif // SIGNALBRIDGE_DEVICE_CONFIG_PAGE_FACTORY_H
