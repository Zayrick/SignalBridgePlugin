#include "hid/HidBackend.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <stdexcept>

#include <QString>

#include "domain/PathUtils.h"
#include "hid/HidReportDescriptor.h"

namespace signalbridge
{
namespace
{
std::string WideToUtf8(const wchar_t* value)
{
    if(value == nullptr || value[0] == L'\0')
    {
        return {};
    }

    return QString::fromWCharArray(value).toStdString();
}

std::string HidError(hid_device* device, const std::string& fallback)
{
    const std::string message = WideToUtf8(hid_error(device));
    return message.empty() ? fallback : message;
}

std::optional<std::string> InstanceKey(const std::string& path)
{
    const std::size_t first = path.find('#');
    if(first == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t second = path.find('#', first + 1);
    if(second == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t third = path.find('#', second + 1);
    return path.substr(second + 1, third == std::string::npos ? std::string::npos : third - second - 1);
}

bool HasCollectionMarker(const std::string& path)
{
    const std::string upper = UpperAscii(path);
    return upper.find("&COL") != std::string::npos ||
           upper.find("#COL") != std::string::npos ||
           upper.find("_COL") != std::string::npos;
}

std::string CollectionIndependentInstance(const std::string& path, std::string instance)
{
    if(!HasCollectionMarker(path))
    {
        return instance;
    }

    const std::size_t suffix = instance.rfind('&');
    if(suffix == std::string::npos || suffix + 1 >= instance.size())
    {
        return instance;
    }

    const bool hex_suffix = std::all_of(
        instance.begin() + static_cast<std::string::difference_type>(suffix + 1),
        instance.end(),
        [](char value) {
            return std::isxdigit(static_cast<unsigned char>(value)) != 0;
        });
    if(hex_suffix)
    {
        instance.erase(suffix);
    }
    return instance;
}

HidInfo FromDeviceInfo(const hid_device_info* device)
{
    HidInfo info;
    info.path = device->path != nullptr ? device->path : "";
    info.vid = device->vendor_id;
    info.pid = device->product_id;
    info.serial = WideToUtf8(device->serial_number);
    info.manufacturer = WideToUtf8(device->manufacturer_string);
    info.product = WideToUtf8(device->product_string);
    info.interface_number = device->interface_number;
    info.usage = device->usage;
    info.usage_page = device->usage_page;
    return info;
}

std::vector<std::uint8_t> ReadReportDescriptor(const std::string& path)
{
    std::unique_ptr<hid_device, decltype(&hid_close)> device(
        hid_open_path(path.c_str()),
        &hid_close);
    if(!device)
    {
        return {};
    }

    std::vector<std::uint8_t> descriptor(HID_API_MAX_REPORT_DESCRIPTOR_SIZE);
    const int size =
        hid_get_report_descriptor(device.get(), descriptor.data(), descriptor.size());
    if(size <= 0)
    {
        return {};
    }

    descriptor.resize(static_cast<std::size_t>(size));
    return descriptor;
}
}

HidBackend::HidBackend()
{
    if(hid_init() != 0)
    {
        throw std::runtime_error("Failed to initialize hidapi");
    }
}

HidBackend::~HidBackend()
{
    CloseAll();
    hid_exit();
}

std::vector<HidInfo> HidBackend::Enumerate(std::optional<std::uint16_t> vid,
                                           std::optional<std::uint16_t> pid) const
{
    const unsigned short vid_filter = vid.value_or(0);
    const unsigned short pid_filter = pid.value_or(0);
    std::lock_guard<std::mutex> lock(mutex_);
    hid_device_info* head = hid_enumerate(vid_filter, pid_filter);
    std::vector<HidInfo> devices;

    for(const hid_device_info* current = head; current != nullptr; current = current->next)
    {
        devices.push_back(FromDeviceInfo(current));
    }

    hid_free_enumeration(head);
    AssignCollectionIndices(devices, ReadReportDescriptor);
    return devices;
}

HidBackend::Handle HidBackend::OpenPath(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    hid_device* device = hid_open_path(path.c_str());
    if(device == nullptr)
    {
        throw std::runtime_error("Failed to open HID device at '" + path + "'");
    }

    return StoreDeviceLocked(device);
}

void HidBackend::Close(Handle handle)
{
    hid_device* device = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = devices_.find(handle);
        if(it == devices_.end())
        {
            return;
        }

        device = it->second;
        devices_.erase(it);
    }

    hid_close(device);
}

void HidBackend::CloseAll()
{
    std::unordered_map<Handle, hid_device*> devices;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        devices.swap(devices_);
    }

    for(auto& device : devices)
    {
        hid_close(device.second);
    }
}

std::size_t HidBackend::Write(Handle handle, const std::vector<std::uint8_t>& data)
{
    if(data.empty())
    {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    hid_device* device = DeviceLocked(handle);
    const int written = hid_write(device, data.data(), data.size());
    if(written < 0)
    {
        throw std::runtime_error("HID write failed: " + HidError(device, "hid_write returned an error"));
    }

    return static_cast<std::size_t>(written);
}

std::vector<std::uint8_t> HidBackend::Read(Handle handle, std::size_t length, int timeout_ms)
{
    if(length == 0)
    {
        return {};
    }

    std::vector<std::uint8_t> buffer(length);
    std::lock_guard<std::mutex> lock(mutex_);
    hid_device* device = DeviceLocked(handle);
    const int read = timeout_ms < 0
                         ? hid_read(device, buffer.data(), buffer.size())
                         : hid_read_timeout(device, buffer.data(), buffer.size(), timeout_ms);
    if(read < 0)
    {
        throw std::runtime_error("HID read failed: " + HidError(device, "hid_read returned an error"));
    }

    buffer.resize(static_cast<std::size_t>(read));
    return buffer;
}

std::size_t HidBackend::SendFeatureReport(Handle handle, const std::vector<std::uint8_t>& data)
{
    if(data.empty())
    {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    hid_device* device = DeviceLocked(handle);
    const int written = hid_send_feature_report(device, data.data(), data.size());
    if(written < 0)
    {
        throw std::runtime_error("HID send_feature_report failed: " + HidError(device, "hid_send_feature_report returned an error"));
    }

    return static_cast<std::size_t>(written);
}

std::vector<std::uint8_t> HidBackend::GetFeatureReport(Handle handle, std::uint8_t report_id, std::size_t length)
{
    if(length == 0)
    {
        return {};
    }

    std::vector<std::uint8_t> buffer(length);
    buffer[0] = report_id;

    std::lock_guard<std::mutex> lock(mutex_);
    hid_device* device = DeviceLocked(handle);
    const int read = hid_get_feature_report(device, buffer.data(), buffer.size());
    if(read < 0)
    {
        throw std::runtime_error("HID get_feature_report failed: " + HidError(device, "hid_get_feature_report returned an error"));
    }

    buffer.resize(static_cast<std::size_t>(read));
    return buffer;
}

std::size_t HidBackend::Flush(Handle handle, std::size_t max_reads)
{
    std::size_t flushed = 0;
    std::vector<std::uint8_t> buffer(1024);

    std::lock_guard<std::mutex> lock(mutex_);
    hid_device* device = DeviceLocked(handle);
    for(std::size_t read_idx = 0; read_idx < max_reads; ++read_idx)
    {
        const int read = hid_read_timeout(device, buffer.data(), buffer.size(), 0);
        if(read < 0)
        {
            throw std::runtime_error("HID flush failed: " + HidError(device, "hid_read_timeout returned an error"));
        }
        if(read == 0)
        {
            break;
        }
        ++flushed;
    }

    return flushed;
}

std::vector<HidInfo> HidBackend::CollectEndpoints(const HidInfo& primary) const
{
    const std::string group = NormalizeDevicePath(primary.path);
    std::vector<HidInfo> endpoints;
    endpoints.push_back(primary);

    const std::vector<HidInfo> devices = Enumerate(primary.vid, primary.pid);
    for(const HidInfo& device : devices)
    {
        const bool same_group = !group.empty() && NormalizeDevicePath(device.path) == group;
        const bool already_added = std::any_of(endpoints.begin(), endpoints.end(), [&](const HidInfo& endpoint) {
            return EndpointKey(endpoint) == EndpointKey(device);
        });

        if(same_group && !already_added)
        {
            endpoints.push_back(device);
        }
    }

    return endpoints;
}

void HidBackend::AssignCollectionIndices(
    std::vector<HidInfo>& devices,
    const ReportDescriptorReader& read_report_descriptor)
{
    using PathDevices = std::map<std::string, std::vector<std::size_t>>;
    std::map<std::string, PathDevices> devices_by_physical_interface;

    for(std::size_t index = 0; index < devices.size(); ++index)
    {
        HidInfo& device = devices[index];
        if(device.path.empty())
        {
            device.collection = 0;
            continue;
        }

        std::string group = NormalizeDevicePath(device.path);
        if(group.empty())
        {
            group = device.path;
        }
        group += ":" + std::to_string(device.interface_number.value_or(-1));
        devices_by_physical_interface[group][device.path].push_back(index);
    }

    for(const auto& interface_group : devices_by_physical_interface)
    {
        int path_collection_offset = 0;
        for(const auto& path_group : interface_group.second)
        {
            std::vector<HidTopLevelCollection> parsed_collections;
            if(read_report_descriptor)
            {
                parsed_collections =
                    ParseHidTopLevelCollections(read_report_descriptor(path_group.first));
            }

            std::size_t collection_slots =
                std::max<std::size_t>(1, path_group.second.size());
            for(const HidTopLevelCollection& collection : parsed_collections)
            {
                if(collection.collection >= 0)
                {
                    collection_slots = std::max(
                        collection_slots,
                        static_cast<std::size_t>(collection.collection) + 1);
                }
            }

            std::vector<bool> used_collections(collection_slots, false);
            for(std::size_t device_index : path_group.second)
            {
                HidInfo& device = devices[device_index];
                int local_collection = -1;
                for(const HidTopLevelCollection& collection : parsed_collections)
                {
                    if(collection.collection < 0 ||
                       static_cast<std::size_t>(collection.collection) >= used_collections.size() ||
                       used_collections[static_cast<std::size_t>(collection.collection)] ||
                       collection.usage != device.usage.value_or(0) ||
                       collection.usage_page != device.usage_page.value_or(0))
                    {
                        continue;
                    }

                    local_collection = collection.collection;
                    break;
                }

                if(local_collection < 0)
                {
                    const auto unused = std::find(
                        used_collections.begin(),
                        used_collections.end(),
                        false);
                    local_collection = unused != used_collections.end()
                                           ? static_cast<int>(std::distance(
                                                 used_collections.begin(),
                                                 unused))
                                           : 0;
                }

                used_collections[static_cast<std::size_t>(local_collection)] = true;
                device.collection = path_collection_offset + local_collection;
            }

            path_collection_offset += static_cast<int>(collection_slots);
        }
    }
}

std::string HidBackend::EndpointKey(const HidInfo& info)
{
    return std::to_string(info.interface_number.value_or(0)) + ":" +
           std::to_string(info.usage.value_or(0)) + ":" +
           std::to_string(info.usage_page.value_or(0)) + ":" +
           std::to_string(info.collection);
}

std::string HidBackend::NormalizeDevicePath(const std::string& path)
{
    const std::string upper = UpperAscii(path);
    const std::size_t vid_pos = upper.find("VID_");
    if(vid_pos == std::string::npos)
    {
        return path;
    }

    const std::size_t pid_pos = upper.find("&PID_", vid_pos);
    if(pid_pos == std::string::npos)
    {
        return path;
    }

    const std::size_t pid_value_pos = pid_pos + 5;
    std::size_t pid_end = pid_value_pos;
    while(pid_end < upper.size() && std::isxdigit(static_cast<unsigned char>(upper[pid_end])) != 0)
    {
        ++pid_end;
    }

    std::string vid_pid = upper.substr(vid_pos, pid_end - vid_pos);
    const std::optional<std::string> instance = InstanceKey(path);
    if(instance.has_value())
    {
        return vid_pid + "#" + UpperAscii(CollectionIndependentInstance(path, *instance));
    }

    return vid_pid;
}

hid_device* HidBackend::DeviceLocked(Handle handle) const
{
    const auto it = devices_.find(handle);
    if(it == devices_.end())
    {
        throw std::runtime_error("Invalid HID handle " + std::to_string(handle));
    }

    return it->second;
}

HidBackend::Handle HidBackend::StoreDeviceLocked(hid_device* device)
{
    if(next_handle_ == 0)
    {
        throw std::runtime_error("HID handle counter overflowed");
    }

    const Handle handle = next_handle_++;
    devices_.insert({ handle, device });
    return handle;
}
}
