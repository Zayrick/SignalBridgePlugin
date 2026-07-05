#ifndef SIGNALBRIDGE_HID_BACKEND_H
#define SIGNALBRIDGE_HID_BACKEND_H

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <hidapi.h>

#include "hid/HidTypes.h"

namespace signalbridge
{
class HidBackend
{
public:
    using Handle = std::uint64_t;

    HidBackend();
    ~HidBackend();

    HidBackend(const HidBackend&) = delete;
    HidBackend& operator=(const HidBackend&) = delete;

    std::vector<HidInfo> Enumerate(std::optional<std::uint16_t> vid = std::nullopt,
                                   std::optional<std::uint16_t> pid = std::nullopt) const;

    Handle OpenPath(const std::string& path);
    void Close(Handle handle);

    std::size_t Write(Handle handle, const std::vector<std::uint8_t>& data);
    std::vector<std::uint8_t> Read(Handle handle, std::size_t length, int timeout_ms);
    std::size_t SendFeatureReport(Handle handle, const std::vector<std::uint8_t>& data);
    std::vector<std::uint8_t> GetFeatureReport(Handle handle, std::uint8_t report_id, std::size_t length);
    std::size_t Flush(Handle handle, std::size_t max_reads = 256);

    std::vector<HidInfo> CollectEndpoints(const HidInfo& primary) const;

    static std::string EndpointKey(const HidInfo& info);
    static std::string NormalizeDevicePath(const std::string& path);

private:
    void CloseAll();
    hid_device* DeviceLocked(Handle handle) const;
    Handle StoreDeviceLocked(hid_device* device);

    mutable std::mutex mutex_;
    std::unordered_map<Handle, hid_device*> devices_;
    Handle next_handle_ = 1;
};
}

#endif // SIGNALBRIDGE_HID_BACKEND_H
