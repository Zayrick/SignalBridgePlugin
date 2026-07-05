#include "scanning/ScriptScanner.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include <QDir>
#include <QDirIterator>
#include <QFile>

#include "domain/PathUtils.h"
#include "scanning/ScriptMetadataExtractor.h"

namespace signalbridge
{
ScanReport ScanDirectory(
    const std::string& script_directory,
    ScanProgressCallback progress_callback,
    ScriptLogCallback log_callback)
{
    ScanReport report;
    const QDir root(QString::fromStdString(script_directory));
    std::vector<QString> paths;

    QDirIterator it(root.absolutePath(), QStringList{ "*.js" }, QDir::Files, QDirIterator::Subdirectories);
    while(it.hasNext())
    {
        paths.push_back(it.next());
    }
    std::sort(paths.begin(), paths.end());

    std::vector<ScriptSource> sources;
    sources.reserve(paths.size());
    for(const QString& path : paths)
    {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            report.errors.push_back({ path.toStdString(), "read error" });
            continue;
        }
        const std::string relative = NormalizeLookupPath(root.relativeFilePath(path).toStdString());
        sources.push_back({
            path.toStdString(),
            relative,
            QString::fromUtf8(file.readAll()).toStdString(),
        });
    }

    std::size_t completed = 0;
    for(const ScriptSource& source : sources)
    {
        try
        {
            std::optional<ScriptMeta> meta = ExtractScriptMetadata(source, sources, log_callback);
            if(meta.has_value())
            {
                report.scripts.push_back(std::move(*meta));
            }
        }
        catch(const std::exception& err)
        {
            report.errors.push_back({ source.source_path, err.what() });
        }

        completed++;
        if(progress_callback)
        {
            progress_callback(completed, sources.size());
        }
    }

    return report;
}
}
