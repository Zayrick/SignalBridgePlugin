#ifndef SIGNALBRIDGE_SCRIPT_METADATA_EXTRACTOR_H
#define SIGNALBRIDGE_SCRIPT_METADATA_EXTRACTOR_H

#include <optional>
#include <vector>

#include "domain/ScriptTypes.h"

namespace signalbridge
{
class ScriptMetadataExtractor
{
public:
    std::optional<ScriptMeta> Extract(
        const ScriptSource& script,
        const std::vector<ScriptSource>& catalog,
        const ScriptLogCallback& log_callback) const;
};
}

#endif // SIGNALBRIDGE_SCRIPT_METADATA_EXTRACTOR_H
