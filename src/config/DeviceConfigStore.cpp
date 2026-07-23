#include "config/DeviceConfigStore.h"

#include <string>

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include "domain/DeviceRecords.h"

namespace signalbridge
{
void DeviceConfigStore::Load(const filesystem::path& configuration_root)
{
    std::lock_guard<std::mutex> lock(mutex_);
    configuration_root_ = configuration_root;
    store_ = QJsonObject();

    try
    {
        QFile file(StorePath());
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if(!document.isObject())
        {
            return;
        }

        const QJsonObject root = document.object();
        store_ = root.value("devices").isObject() ? root.value("devices").toObject() : root;
    }
    catch(...)
    {
        store_ = QJsonObject();
    }
}

void DeviceConfigStore::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    configuration_root_.clear();
    store_ = QJsonObject();
}

QJsonObject DeviceConfigStore::ConfigurationForDevice(const QString& key, const QString& script_key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    QJsonObject configuration = store_.value(script_key).toObject();
    const QJsonObject exact = store_.value(key).toObject();
    for(auto it = exact.begin(); it != exact.end(); ++it)
    {
        configuration.insert(it.key(), it.value());
    }
    return configuration;
}

QJsonObject DeviceConfigStore::ConfigurationForDevice(const QString& key, const ScriptMeta& meta) const
{
    return ConfigurationForDevice(key, ConfigKeyForScript(meta));
}

bool DeviceConfigStore::SetDeviceConfigurationValue(const QString& key, const QString& property, const QJsonValue& value)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(key.isEmpty() || property.isEmpty())
        {
            return false;
        }

        QJsonObject device = store_.value(key).toObject();
        if(device.contains(property) && device.value(property) == value)
        {
            return false;
        }

        device.insert(property, value);
        store_.insert(key, device);
    }

    Save();
    return true;
}

QString DeviceConfigStore::StorePath() const
{
    if(configuration_root_.empty())
    {
        return QString();
    }

    const filesystem::path directory = configuration_root_ / "SignalBridge";
    filesystem::create_directories(directory);
    const auto path = (directory / "device_config.json").generic_u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(path.data()),
        static_cast<int>(path.size()));
}

bool DeviceConfigStore::Save() const
{
    QJsonObject snapshot;
    QString path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = store_;
        path = StorePath();
    }

    try
    {
        if(path.isEmpty())
        {
            return false;
        }

        QJsonObject root;
        root.insert("version", 1);
        root.insert("devices", snapshot);

        QSaveFile file(path);
        if(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
            if(file.write(bytes) == bytes.size())
            {
                return file.commit();
            }
        }
    }
    catch(...)
    {
    }
    return false;
}
}
