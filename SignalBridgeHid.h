#ifndef SIGNALBRIDGEHID_H
#define SIGNALBRIDGEHID_H

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <hidapi.h>

struct SignalBridgeHidInfo
{
    std::string path;
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    std::string serial;
    std::string manufacturer;
    std::string product;
    std::optional<int> interface_number;
    std::optional<std::uint16_t> usage;
    std::optional<std::uint16_t> usage_page;
};

class SignalBridgeHidBackend
{
public:
    using Handle = std::uint64_t;

    SignalBridgeHidBackend();
    ~SignalBridgeHidBackend();

    SignalBridgeHidBackend(const SignalBridgeHidBackend&) = delete;
    SignalBridgeHidBackend& operator=(const SignalBridgeHidBackend&) = delete;

    std::vector<SignalBridgeHidInfo> Enumerate(std::optional<std::uint16_t> vid = std::nullopt,
                                               std::optional<std::uint16_t> pid = std::nullopt) const;

    Handle OpenPath(const std::string& path);
    void Close(Handle handle);
    void CloseAll();

    std::size_t Write(Handle handle, const std::vector<std::uint8_t>& data);
    std::vector<std::uint8_t> Read(Handle handle, std::size_t length, int timeout_ms);
    std::size_t SendFeatureReport(Handle handle, const std::vector<std::uint8_t>& data);
    std::vector<std::uint8_t> GetFeatureReport(Handle handle, std::uint8_t report_id, std::size_t length);
    std::size_t Flush(Handle handle, std::size_t max_reads = 256);

    std::vector<SignalBridgeHidInfo> CollectEndpoints(const SignalBridgeHidInfo& primary) const;

    static std::string EndpointKey(const SignalBridgeHidInfo& info);
    static std::string NormalizeDevicePath(const std::string& path);
    static std::string MakeControllerPort(const SignalBridgeHidInfo& info);

private:
    hid_device* DeviceLocked(Handle handle) const;
    Handle StoreDeviceLocked(hid_device* device);

    mutable std::mutex mutex_;
    std::unordered_map<Handle, hid_device*> devices_;
    Handle next_handle_ = 1;
};

#endif // SIGNALBRIDGEHID_H
