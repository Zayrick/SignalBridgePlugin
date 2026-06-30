#ifndef SIGNALBRIDGE_SCRIPT_SCANNER_H
#define SIGNALBRIDGE_SCRIPT_SCANNER_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "domain/ScriptTypes.h"

namespace signalbridge
{
struct ScanError
{
    std::string path;
    std::string error;
};

struct ScanReport
{
    std::vector<ScriptMeta> scripts;
    std::vector<ScanError> errors;
};

using ScanProgressCallback = std::function<void(std::size_t completed, std::size_t total, const std::string& path)>;

class ScriptScanner
{
public:
    ScanReport ScanDirectory(
        const std::string& script_directory,
        ScanProgressCallback progress_callback = {},
        ScriptLogCallback log_callback = {}) const;
};
}

#endif // SIGNALBRIDGE_SCRIPT_SCANNER_H
