#include "runtime/signalrgb/SystemInfoModule.h"

#include <QDate>
#include <QJsonArray>
#include <QSettings>
#include <QStringList>
#include <QVariant>

#include "runtime/QuickJsValue.h"
#include "runtime/RuntimeBindingUtils.h"
#include "runtime/signalrgb/SignalRgbModuleRegistry.h"

namespace signalbridge
{
namespace
{
QString ReadSettingsString(QSettings& settings, const char* key)
{
    const QVariant value = settings.value(QString::fromLatin1(key));
    if(!value.isValid() || value.isNull())
    {
        return QString();
    }

    QString text = value.toString().trimmed();
    if(!text.isEmpty())
    {
        return text;
    }

    const QStringList values = value.toStringList();
    for(const QString& item : values)
    {
        const QString trimmed = item.trimmed();
        if(!trimmed.isEmpty())
        {
            if(!text.isEmpty())
            {
                text += QStringLiteral(" ");
            }
            text += trimmed;
        }
    }
    return text;
}

QString NormalizeBiosDate(const QString& raw)
{
    const QString text = raw.trimmed();
    if(text.isEmpty())
    {
        return QString();
    }

    if(text.size() >= 8)
    {
        bool digits = true;
        for(int idx = 0; idx < 8; idx++)
        {
            if(!text.at(idx).isDigit())
            {
                digits = false;
                break;
            }
        }
        if(digits)
        {
            const QDate date = QDate::fromString(text.left(8), QStringLiteral("yyyyMMdd"));
            if(date.isValid())
            {
                return date.toString(Qt::ISODate);
            }
        }
    }

    const QStringList formats = {
        QStringLiteral("MM/dd/yyyy"),
        QStringLiteral("M/d/yyyy"),
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd.MM.yyyy"),
    };
    for(const QString& format : formats)
    {
        const QDate date = QDate::fromString(text, format);
        if(date.isValid())
        {
            return date.toString(Qt::ISODate);
        }
    }
    return text;
}

JSValue GetMotherboardInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JsonToJsValue(context, MotherboardInfo(), "<systeminfo-motherboard>");
}

JSValue GetBiosInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JsonToJsValue(context, BiosInfo(), "<systeminfo-bios>");
}

JSValue GetRamInfoJs(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JsonToJsValue(context, RamInfo(), "<systeminfo-ram>");
}

JSValue CreateSystemInfoObject(JSContext* context)
{
    JSValue object = JS_NewObject(context);
    SetFunctionProperty(context, object, "GetMotherboardInfo", GetMotherboardInfoJs);
    SetFunctionProperty(context, object, "GetBiosInfo", GetBiosInfoJs);
    SetFunctionProperty(context, object, "GetRamInfo", GetRamInfoJs);
    return object;
}

int SystemInfoModuleInit(JSContext* context, JSModuleDef* module)
{
    JSValue object = GetOrCreateGlobalProperty(context, "systeminfo", CreateSystemInfoObject);
    JSValue named = JS_DupValue(context, object);
    if(JS_SetModuleExport(context, module, "systeminfo", named) < 0)
    {
        JS_FreeValue(context, named);
        JS_FreeValue(context, object);
        return -1;
    }
    if(JS_SetModuleExport(context, module, "default", object) < 0)
    {
        JS_FreeValue(context, object);
        return -1;
    }
    return 0;
}
}

QJsonObject MotherboardInfo()
{
    QString manufacturer;
    QString product;
    QString version;

#ifdef Q_OS_WIN
    QSettings bios(
        QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"),
        QSettings::NativeFormat);
    manufacturer = ReadSettingsString(bios, "BaseBoardManufacturer");
    product = ReadSettingsString(bios, "BaseBoardProduct");
    version = ReadSettingsString(bios, "BaseBoardVersion");
#endif

    QJsonObject info;
    info.insert(QStringLiteral("model"), product);
    info.insert(QStringLiteral("manufacturer"), manufacturer);
    info.insert(QStringLiteral("product"), product);
    info.insert(QStringLiteral("vendor"), manufacturer);
    if(!version.isEmpty())
    {
        info.insert(QStringLiteral("version"), version);
    }
    return info;
}

QJsonObject BiosInfo()
{
    QString vendor;
    QString version;
    QString raw_date;

#ifdef Q_OS_WIN
    QSettings bios(
        QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"),
        QSettings::NativeFormat);
    vendor = ReadSettingsString(bios, "BIOSVendor");
    version = ReadSettingsString(bios, "BIOSVersion");
    raw_date = ReadSettingsString(bios, "BIOSReleaseDate");
#endif

    const QString release_date = NormalizeBiosDate(raw_date);
    QJsonObject info;
    info.insert(QStringLiteral("vendor"), vendor);
    info.insert(QStringLiteral("version"), version);
    info.insert(QStringLiteral("date"), raw_date);
    info.insert(QStringLiteral("releaseDate"), release_date);
    return info;
}

QJsonObject RamInfo()
{
    QJsonObject info;
    info.insert(QStringLiteral("totalMemory"), 0);
    info.insert(QStringLiteral("modules"), QJsonArray());
    return info;
}

namespace
{
void RegisterSystemInfoGlobal(JSContext* context)
{
    SetGlobalProperty(context, "systeminfo", CreateSystemInfoObject(context));
}

JSModuleDef* LoadSystemInfoModule(JSContext* context, const char* module_name)
{
    JSModuleDef* module = JS_NewCModule(context, module_name, SystemInfoModuleInit);
    if(module == nullptr)
    {
        return nullptr;
    }
    if(JS_AddModuleExport(context, module, "systeminfo") < 0 ||
       JS_AddModuleExport(context, module, "default") < 0)
    {
        return nullptr;
    }
    return module;
}

[[maybe_unused]] const bool kRegistered = RegisterSignalRgbModule({
    "@SignalRGB/systeminfo",
    LoadSystemInfoModule,
    RegisterSystemInfoGlobal,
});
}
}
