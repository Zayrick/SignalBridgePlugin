#include "runtime/BuiltinModules.h"

#include <stdexcept>

#include <QFile>
#include <QString>

namespace signalbridge
{
namespace
{
bool StartsWith(const std::string& value, const char* prefix)
{
    const std::string expected(prefix);
    return value.size() >= expected.size() && value.substr(0, expected.size()) == expected;
}

std::string BuiltinResourcePath(const std::string& normalized)
{
    return normalized + ".js";
}
}

std::string NormalizeBuiltinSpecifier(std::string specifier)
{
    if(specifier.size() > 3 && specifier.substr(specifier.size() - 3) == ".js")
    {
        specifier.resize(specifier.size() - 3);
    }
    return specifier;
}

bool IsBuiltinModule(const std::string& specifier)
{
    const std::string normalized = NormalizeBuiltinSpecifier(specifier);
    return StartsWith(normalized, "@SignalRGB/");
}

std::string BuiltinModuleSource(const std::string& specifier)
{
    const std::string normalized = NormalizeBuiltinSpecifier(specifier);
    if(!IsBuiltinModule(normalized))
    {
        return {};
    }
    return LoadRuntimeResourceText(BuiltinResourcePath(normalized));
}

std::string LoadRuntimeResourceText(const std::string& relative_path)
{
    const QString path = QStringLiteral(":/SignalBridge/") + QString::fromStdString(relative_path);
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        throw std::runtime_error("Failed to read " + path.toStdString());
    }
    return QString::fromUtf8(file.readAll()).toStdString();
}
}
