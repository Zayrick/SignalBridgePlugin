#include "openrgb/EndpointSession.h"

#include <utility>

namespace signalbridge
{
EndpointSession::EndpointSession(std::shared_ptr<HidBackend> backend, HidInfo primary_hid)
    : backend_(std::move(backend))
    , primary_hid_(std::move(primary_hid))
{
    Open();
}

EndpointSession::~EndpointSession()
{
    Close();
}

HidBackend::Handle EndpointSession::PrimaryHandle() const
{
    return primary_handle_;
}

const std::map<std::string, HidBackend::Handle>& EndpointSession::Handles() const
{
    return endpoint_handles_;
}

const std::vector<EndpointDescriptor>& EndpointSession::Endpoints() const
{
    return endpoints_;
}

void EndpointSession::Open()
{
    primary_handle_ = backend_->OpenPath(primary_hid_.path);
    endpoint_handles_[HidBackend::EndpointKey(primary_hid_)] = primary_handle_;

    const std::vector<HidInfo> endpoints = backend_->CollectEndpoints(primary_hid_);
    for(const HidInfo& endpoint : endpoints)
    {
        const std::string key = HidBackend::EndpointKey(endpoint);
        endpoints_.push_back({
            endpoint.interface_number.value_or(0),
            endpoint.usage.value_or(0),
            endpoint.usage_page.value_or(0),
            endpoint.collection,
        });

        if(endpoint_handles_.find(key) != endpoint_handles_.end())
        {
            continue;
        }

        try
        {
            endpoint_handles_[key] = backend_->OpenPath(endpoint.path);
        }
        catch(...)
        {
        }
    }
}

void EndpointSession::Close()
{
    for(const auto& item : endpoint_handles_)
    {
        try
        {
            backend_->Close(item.second);
        }
        catch(...)
        {
        }
    }
    endpoint_handles_.clear();
    endpoints_.clear();
    primary_handle_ = 0;
}
}
