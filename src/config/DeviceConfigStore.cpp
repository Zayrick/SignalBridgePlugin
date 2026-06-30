#include "config/DeviceConfigStore.h"

#include <QFile>
#include <QJsonDocument>

#include "domain/DeviceRecords.h"

namespace signalbridge
{
void DeviceConfigStore::Load(ResourceManagerInterface* manager)
{
    std::lock_guard<std::mutex> lock(mutex_);
    manager_ = manager;
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
    manager_ = nullptr;
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

void DeviceConfigStore::SetDeviceConfigurationValue(const QString& key, const QString& property, const QJsonValue& value)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        QJsonObject device = store_.value(key).toObject();
        device.insert(property, value);
        store_.insert(key, device);
    }

    Save();
}

QString DeviceConfigStore::StorePath() const
{
    if(manager_ == nullptr)
    {
        return QString();
    }

    const filesystem::path directory = manager_->GetConfigurationDirectory() / "SignalBridge";
    filesystem::create_directories(directory);
    return QString::fromStdString((directory / "device_config.json").generic_u8string());
}

void DeviceConfigStore::Save() const
{
    QJsonObject snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = store_;
    }

    try
    {
        const QString path = StorePath();
        if(path.isEmpty())
        {
            return;
        }

        QJsonObject root;
        root.insert("version", 1);
        root.insert("devices", snapshot);

        QFile file(path);
        if(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        }
    }
    catch(...)
    {
    }
}
}
