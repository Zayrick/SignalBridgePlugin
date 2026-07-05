#ifndef SIGNALBRIDGE_SCRIPT_METADATA_EXTRACTOR_H
#define SIGNALBRIDGE_SCRIPT_METADATA_EXTRACTOR_H

#include <optional>
#include <vector>

#include "domain/ScriptTypes.h"

namespace signalbridge
{
std::optional<ScriptMeta> ExtractScriptMetadata(
    const ScriptSource& script,
    const std::vector<ScriptSource>& catalog,
    const ScriptLogCallback& log_callback);
}

#endif // SIGNALBRIDGE_SCRIPT_METADATA_EXTRACTOR_H
