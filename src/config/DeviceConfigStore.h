#ifndef SIGNALBRIDGE_DEVICE_CONFIG_STORE_H
#define SIGNALBRIDGE_DEVICE_CONFIG_STORE_H

#include <mutex>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "filesystem.h"
#include "domain/ScriptTypes.h"

namespace signalbridge
{
class DeviceConfigStore
{
public:
    void Load(const filesystem::path& configuration_root);
    void Reset();

    QJsonObject ConfigurationForDevice(const QString& key, const QString& script_key) const;
    QJsonObject ConfigurationForDevice(const QString& key, const ScriptMeta& meta) const;
    bool SetDeviceConfigurationValue(const QString& key, const QString& property, const QJsonValue& value);

private:
    QString StorePath() const;
    bool Save() const;

    filesystem::path configuration_root_;
    mutable std::mutex mutex_;
    QJsonObject store_;
};
}

#endif // SIGNALBRIDGE_DEVICE_CONFIG_STORE_H
