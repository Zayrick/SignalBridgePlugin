#ifndef SIGNALBRIDGESCRIPTSCANNER_H
#define SIGNALBRIDGESCRIPTSCANNER_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "SignalBridgeScriptRuntime.h"

struct SignalBridgeScanError
{
    std::string path;
    std::string error;
};

struct SignalBridgeScanReport
{
    std::vector<SignalBridgeScriptMeta> scripts;
    std::vector<SignalBridgeScanError> errors;
};

using SignalBridgeScanProgressCallback = std::function<void(std::size_t completed, std::size_t total, const std::string& path)>;

class SignalBridgeScriptScanner
{
public:
    SignalBridgeScanReport ScanDirectory(
        const std::string& script_directory,
        SignalBridgeScanProgressCallback progress_callback = {}) const;

private:
    std::optional<SignalBridgeScriptMeta> ScanScript(
        const SignalBridgeScriptSource& script,
        const std::vector<SignalBridgeScriptSource>& catalog) const;
};

#endif // SIGNALBRIDGESCRIPTSCANNER_H
