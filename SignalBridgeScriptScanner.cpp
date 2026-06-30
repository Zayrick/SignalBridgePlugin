#include "SignalBridgeScriptScanner.h"

#include <algorithm>
#include <set>
#include <stdexcept>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace
{
struct ImportDecl
{
    QString specifier;
    QString default_alias;
    QString namespace_alias;
    std::vector<std::pair<QString, QString>> named_aliases;
};

struct ExportBindings
{
    std::vector<QString> named;
    QString default_name;
};

QString NormalizeLookupPath(QString path)
{
    path.replace('\\', '/');
    QStringList parts;
    for(const QString& part : path.split('/'))
    {
        if(part.isEmpty() || part == ".")
        {
            continue;
        }
        if(part == "..")
        {
            if(!parts.isEmpty())
            {
                parts.removeLast();
            }
            continue;
        }
        parts.append(part);
    }
    return parts.join('/');
}

QString LookupDir(const QString& path)
{
    const QString normalized = NormalizeLookupPath(path);
    const int slash = normalized.lastIndexOf('/');
    return slash >= 0 ? normalized.left(slash) : QString();
}

bool IsJsIdentifier(const QString& value)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_$][A-Za-z0-9_$]*$"));
    return re.match(value).hasMatch();
}

QString JsonString(const QString& value)
{
    const QByteArray wrapped = QJsonDocument(QJsonArray{ value }).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}

void ParseNamedImports(const QString& clause, ImportDecl& import)
{
    const int start = clause.indexOf('{');
    const int end = clause.lastIndexOf('}');
    if(start < 0 || end <= start)
    {
        return;
    }

    for(const QString& raw_item : clause.mid(start + 1, end - start - 1).split(','))
    {
        const QString item = raw_item.trimmed();
        if(item.isEmpty())
        {
            continue;
        }
        const QStringList alias_parts = item.split(QStringLiteral(" as "));
        const QString imported = alias_parts.value(0).trimmed();
        const QString local = alias_parts.size() > 1 ? alias_parts.value(1).trimmed() : imported;
        if(IsJsIdentifier(imported) && IsJsIdentifier(local))
        {
            import.named_aliases.push_back({ imported, local });
        }
    }
}

ImportDecl ParseImportClause(const QString& clause)
{
    ImportDecl import;
    const QString trimmed = clause.trimmed();
    if(trimmed.startsWith('{'))
    {
        ParseNamedImports(trimmed, import);
        return import;
    }

    const int comma = trimmed.indexOf(',');
    if(comma >= 0)
    {
        const QString default_alias = trimmed.left(comma).trimmed();
        if(IsJsIdentifier(default_alias))
        {
            import.default_alias = default_alias;
        }

        const QString rest = trimmed.mid(comma + 1).trimmed();
        if(rest.startsWith('{'))
        {
            ParseNamedImports(rest, import);
        }
        else if(rest.startsWith(QStringLiteral("* as ")))
        {
            const QString namespace_alias = rest.mid(5).trimmed();
            if(IsJsIdentifier(namespace_alias))
            {
                import.namespace_alias = namespace_alias;
            }
        }
        return import;
    }

    if(trimmed.startsWith(QStringLiteral("* as ")))
    {
        const QString namespace_alias = trimmed.mid(5).trimmed();
        if(IsJsIdentifier(namespace_alias))
        {
            import.namespace_alias = namespace_alias;
        }
    }
    else if(IsJsIdentifier(trimmed))
    {
        import.default_alias = trimmed;
    }
    return import;
}

std::vector<ImportDecl> ParseImportDeclarations(const QString& source)
{
    static const QRegularExpression from_import_re(
        QStringLiteral(R"((?m)^\s*import\s+(.+?)\s+from\s+["']([^"']+)["']\s*;?\s*$)"));
    static const QRegularExpression side_effect_re(
        QStringLiteral(R"((?m)^\s*import\s+["']([^"']+)["']\s*;?\s*$)"));

    std::vector<ImportDecl> imports;
    QRegularExpressionMatchIterator it = from_import_re.globalMatch(source);
    while(it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        ImportDecl import = ParseImportClause(match.captured(1));
        import.specifier = match.captured(2);
        imports.push_back(import);
    }

    it = side_effect_re.globalMatch(source);
    while(it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        ImportDecl import;
        import.specifier = match.captured(1);
        imports.push_back(import);
    }
    return imports;
}

QString PreprocessJs(QString source)
{
    const std::pair<QRegularExpression, QString> replacements[] = {
        { QRegularExpression(QStringLiteral(R"((?m)^\s*import\s+.+?\s+from\s+["'][^"']*["']\s*;?\s*$)")), QString() },
        { QRegularExpression(QStringLiteral(R"((?m)^\s*import\s+\{[^}]*\}\s+from\s+["'][^"']*["']\s*;?\s*$)")), QString() },
        { QRegularExpression(QStringLiteral(R"((?m)^\s*import\s+\S+\s+from\s+["'][^"']*["']\s*;?\s*$)")), QString() },
        { QRegularExpression(QStringLiteral(R"((?m)^\s*import\s+["'][^"']*["']\s*;?\s*$)")), QString() },
        { QRegularExpression(QStringLiteral(R"(\bexport\s+function\s+)")), QStringLiteral("function ") },
        { QRegularExpression(QStringLiteral(R"(\bexport\s+class\s+)")), QStringLiteral("class ") },
        { QRegularExpression(QStringLiteral(R"(\bexport\s+const\s+)")), QStringLiteral("const ") },
        { QRegularExpression(QStringLiteral(R"(\bexport\s+let\s+)")), QStringLiteral("let ") },
        { QRegularExpression(QStringLiteral(R"(\bexport\s+var\s+)")), QStringLiteral("var ") },
        { QRegularExpression(QStringLiteral(R"(\bexport\s+default\s+)")), QString() },
        { QRegularExpression(QStringLiteral(R"((?m)^\s*export\s+\{[^}]*\}\s*;?\s*$)")), QString() },
    };

    for(const auto& replacement : replacements)
    {
        source.replace(replacement.first, replacement.second);
    }
    return source;
}

void PushUnique(std::vector<QString>& values, const QString& value)
{
    if(!value.isEmpty() && std::find(values.begin(), values.end(), value) == values.end())
    {
        values.push_back(value);
    }
}

ExportBindings CollectExportBindings(const QString& source)
{
    static const QRegularExpression export_re(
        QStringLiteral(R"(\bexport\s+(?:async\s+)?(?:function|class|const|let|var)\s+([A-Za-z_$][A-Za-z0-9_$]*))"));
    static const QRegularExpression default_re(
        QStringLiteral(R"(\bexport\s+default\s+(?:async\s+)?(?:function|class)\s+([A-Za-z_$][A-Za-z0-9_$]*))"));
    static const QRegularExpression named_block_re(
        QStringLiteral(R"((?m)^\s*export\s+\{([^}]*)\}\s*;?\s*$)"));

    ExportBindings exports;
    QRegularExpressionMatchIterator it = export_re.globalMatch(source);
    while(it.hasNext())
    {
        PushUnique(exports.named, it.next().captured(1));
    }

    it = default_re.globalMatch(source);
    while(it.hasNext())
    {
        const QString name = it.next().captured(1);
        if(IsJsIdentifier(name))
        {
            exports.default_name = name;
            PushUnique(exports.named, name);
        }
    }

    it = named_block_re.globalMatch(source);
    while(it.hasNext())
    {
        const QString block = it.next().captured(1);
        for(const QString& raw_item : block.split(','))
        {
            const QString item = raw_item.trimmed();
            if(item.isEmpty())
            {
                continue;
            }
            const QStringList alias_parts = item.split(QStringLiteral(" as "));
            const QString local = alias_parts.size() > 1 ? alias_parts.value(1).trimmed() : item;
            if(IsJsIdentifier(local))
            {
                PushUnique(exports.named, local);
            }
        }
    }

    return exports;
}

const SignalBridgeScriptSource* ResolveRelative(
    const std::vector<SignalBridgeScriptSource>& catalog,
    const QString& base_lookup_path,
    const QString& specifier)
{
    if(!specifier.startsWith("./") && !specifier.startsWith("../"))
    {
        return nullptr;
    }

    const QString candidate = NormalizeLookupPath(LookupDir(base_lookup_path) + "/" + specifier);
    const QString with_js = candidate.endsWith(".js", Qt::CaseInsensitive) ? candidate : candidate + ".js";
    for(const SignalBridgeScriptSource& source : catalog)
    {
        const QString lookup = QString::fromStdString(source.lookup_path);
        if(lookup.compare(candidate, Qt::CaseInsensitive) == 0 ||
           lookup.compare(with_js, Qt::CaseInsensitive) == 0)
        {
            return &source;
        }
    }
    return nullptr;
}

QString WrapDependencyModule(const SignalBridgeScriptSource& script)
{
    const QString source = QString::fromStdString(script.source);
    const ExportBindings exports = CollectExportBindings(source);

    QString out = "\n(function() {\n";
    out += PreprocessJs(source);
    out += "\n";
    for(const QString& name : exports.named)
    {
        out += "try { globalThis[";
        out += JsonString(name);
        out += "] = ";
        out += name;
        out += "; } catch (_) {}\n";
    }
    if(!exports.default_name.isEmpty())
    {
        out += "try { globalThis.__srgb_default_export = ";
        out += exports.default_name;
        out += "; } catch (_) {}\n";
    }
    out += "})();\n";
    return out;
}

QString ImportAliasAssignments(const ImportDecl& import, const SignalBridgeScriptSource& dependency)
{
    const ExportBindings exports = CollectExportBindings(QString::fromStdString(dependency.source));
    QString out;

    if(!import.default_alias.isEmpty() && !exports.default_name.isEmpty())
    {
        out += "try { globalThis[";
        out += JsonString(import.default_alias);
        out += "] = globalThis[";
        out += JsonString(exports.default_name);
        out += "] || globalThis.__srgb_default_export; } catch (_) {}\n";
    }
    if(!import.namespace_alias.isEmpty())
    {
        out += "try { globalThis[";
        out += JsonString(import.namespace_alias);
        out += "] = globalThis; } catch (_) {}\n";
    }
    for(const auto& alias : import.named_aliases)
    {
        if(alias.first != alias.second)
        {
            out += "try { globalThis[";
            out += JsonString(alias.second);
            out += "] = globalThis[";
            out += JsonString(alias.first);
            out += "]; } catch (_) {}\n";
        }
    }
    return out;
}

void AppendRelativeImports(
    const SignalBridgeScriptSource& script,
    const std::vector<SignalBridgeScriptSource>& catalog,
    std::set<std::string>& visited,
    QString& out)
{
    const QString source = QString::fromStdString(script.source);
    const QString lookup_path = QString::fromStdString(script.lookup_path);
    for(const ImportDecl& import : ParseImportDeclarations(source))
    {
        const SignalBridgeScriptSource* dependency = ResolveRelative(catalog, lookup_path, import.specifier);
        if(dependency == nullptr)
        {
            if(import.specifier.startsWith("./") || import.specifier.startsWith("../"))
            {
                throw std::runtime_error(("relative import not found: " + import.specifier + " from " + lookup_path).toStdString());
            }
            continue;
        }

        if(visited.insert(dependency->lookup_path).second)
        {
            AppendRelativeImports(*dependency, catalog, visited, out);
            out += WrapDependencyModule(*dependency);
        }
        out += ImportAliasAssignments(import, *dependency);
    }
}

std::optional<std::uint16_t> ValueToU16(const QJsonValue& value)
{
    bool ok = false;
    int parsed = 0;
    if(value.isDouble())
    {
        parsed = static_cast<int>(value.toDouble());
        ok = true;
    }
    else if(value.isString())
    {
        QString text = value.toString().trimmed();
        if(text.startsWith("0x", Qt::CaseInsensitive))
        {
            parsed = text.mid(2).toInt(&ok, 16);
        }
        else
        {
            parsed = text.toInt(&ok, 10);
        }
    }

    if(!ok || parsed < 0 || parsed > 0xFFFF)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::vector<std::uint16_t> ExtractPidList(const QJsonValue& value)
{
    if(const std::optional<std::uint16_t> pid = ValueToU16(value))
    {
        return { *pid };
    }

    std::vector<std::uint16_t> pids;
    for(const QJsonValue& item : value.toArray())
    {
        if(const std::optional<std::uint16_t> pid = ValueToU16(item))
        {
            pids.push_back(*pid);
        }
    }
    return pids;
}

std::vector<std::string> ExtractStringArray(const QJsonValue& value)
{
    std::vector<std::string> result;
    for(const QJsonValue& item : value.toArray())
    {
        result.push_back(item.toString().toStdString());
    }
    return result;
}

std::vector<std::pair<int, int>> ExtractPositions(const QJsonValue& value)
{
    std::vector<std::pair<int, int>> result;
    for(const QJsonValue& item : value.toArray())
    {
        if(item.isArray())
        {
            const QJsonArray array = item.toArray();
            if(array.size() >= 2)
            {
                result.push_back({ array.at(0).toInt(), array.at(1).toInt() });
            }
        }
        else if(item.isObject())
        {
            const QJsonObject object = item.toObject();
            result.push_back({ object.value("x").toInt(), object.value("y").toInt() });
        }
    }
    return result;
}

unsigned int JsonUInt(const QJsonValue& value, unsigned int fallback)
{
    if(!value.isDouble())
    {
        return fallback;
    }
    const double raw = value.toDouble();
    if(raw < 0.0)
    {
        return fallback;
    }
    return static_cast<unsigned int>(raw);
}
}

SignalBridgeScanReport SignalBridgeScriptScanner::ScanDirectory(
    const std::string& script_directory,
    SignalBridgeScanProgressCallback progress_callback) const
{
    SignalBridgeScanReport report;
    const QDir root(QString::fromStdString(script_directory));
    std::vector<QString> paths;

    QDirIterator it(root.absolutePath(), QStringList{ "*.js" }, QDir::Files, QDirIterator::Subdirectories);
    while(it.hasNext())
    {
        paths.push_back(it.next());
    }
    std::sort(paths.begin(), paths.end());

    std::vector<SignalBridgeScriptSource> sources;
    sources.reserve(paths.size());
    for(const QString& path : paths)
    {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            report.errors.push_back({ path.toStdString(), "read error" });
            continue;
        }
        const QString relative = NormalizeLookupPath(root.relativeFilePath(path));
        sources.push_back({
            path.toStdString(),
            relative.toStdString(),
            QString::fromUtf8(file.readAll()).toStdString(),
        });
    }

    std::size_t completed = 0;
    for(const SignalBridgeScriptSource& source : sources)
    {
        try
        {
            report.scripts.push_back(ScanScript(source, sources));
        }
        catch(const std::exception& err)
        {
            report.errors.push_back({ source.source_path, err.what() });
        }

        completed++;
        if(progress_callback)
        {
            progress_callback(completed, sources.size(), source.source_path);
        }
    }

    return report;
}

SignalBridgeScriptMeta SignalBridgeScriptScanner::ScanScript(
    const SignalBridgeScriptSource& script,
    const std::vector<SignalBridgeScriptSource>& catalog) const
{
    if(script.source.find("function Name") == std::string::npos)
    {
        throw std::runtime_error("no Name() function found");
    }

    SignalBridgeScriptMeta meta;
    meta.source_path = script.source_path;
    meta.lookup_path = script.lookup_path;
    meta.js_source = BundleScript(script, catalog);

    SignalBridgeJsRuntime runtime = SignalBridgeJsRuntime::CreateScan();
    runtime.Eval(meta.js_source, script.source_path);

    meta.name = runtime.CallGlobalJson("Name").toString().toStdString();
    if(meta.name.empty())
    {
        throw std::runtime_error("Name() did not return a string");
    }

    meta.vid = ValueToU16(runtime.CallGlobalJson("VendorId"));
    meta.pids = ExtractPidList(runtime.CallGlobalJson("ProductId"));

    const QJsonArray size = runtime.CallGlobalJson("Size").toArray();
    meta.width = size.size() > 0 ? std::max(1u, JsonUInt(size.at(0), 1)) : 1;
    meta.height = size.size() > 1 ? std::max(1u, JsonUInt(size.at(1), 1)) : 1;
    meta.device_type = runtime.CallGlobalJson("DeviceType").toString().toStdString();
    meta.publisher = runtime.CallGlobalJson("Publisher").toString().toStdString();
    meta.image_url = runtime.CallGlobalJson("ImageUrl").toString().toStdString();
    meta.led_names = ExtractStringArray(runtime.CallGlobalJson("LedNames"));
    meta.led_positions = ExtractPositions(runtime.CallGlobalJson("LedPositions"));
    meta.has_validate = runtime.HasGlobal("Validate");

    return meta;
}

std::string SignalBridgeScriptScanner::BundleScript(
    const SignalBridgeScriptSource& script,
    const std::vector<SignalBridgeScriptSource>& catalog) const
{
    std::set<std::string> visited;
    QString bundled;
    AppendRelativeImports(script, catalog, visited, bundled);
    bundled += PreprocessJs(QString::fromStdString(script.source));
    return bundled.toStdString();
}
