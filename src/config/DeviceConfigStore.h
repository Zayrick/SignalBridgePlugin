#ifndef SIGNALBRIDGE_DEVICE_CONFIG_STORE_H
#define SIGNALBRIDGE_DEVICE_CONFIG_STORE_H

#include <mutex>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "ResourceManagerInterface.h"
#include "domain/ScriptTypes.h"

namespace signalbridge
{
class DeviceConfigStore
{
public:
    void Load(ResourceManagerInterface* manager);
    void Reset();

    QJsonObject ConfigurationForDevice(const QString& key, const QString& script_key) const;
    QJsonObject ConfigurationForDevice(const QString& key, const ScriptMeta& meta) const;
    void SetDeviceConfigurationValue(const QString& key, const QString& property, const QJsonValue& value);

private:
    QString StorePath() const;
    void Save() const;

    ResourceManagerInterface* manager_ = nullptr;
    mutable std::mutex mutex_;
    QJsonObject store_;
};
}

#endif // SIGNALBRIDGE_DEVICE_CONFIG_STORE_H
