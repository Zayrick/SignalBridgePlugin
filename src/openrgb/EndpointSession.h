#ifndef SIGNALBRIDGE_ENDPOINT_SESSION_H
#define SIGNALBRIDGE_ENDPOINT_SESSION_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "domain/ScriptTypes.h"
#include "hid/HidBackend.h"

namespace signalbridge
{
class EndpointSession
{
public:
    EndpointSession(std::shared_ptr<HidBackend> backend, HidInfo primary_hid);
    ~EndpointSession();

    EndpointSession(const EndpointSession&) = delete;
    EndpointSession& operator=(const EndpointSession&) = delete;

    HidBackend::Handle PrimaryHandle() const;
    const std::map<std::string, HidBackend::Handle>& Handles() const;
    const std::vector<EndpointDescriptor>& Endpoints() const;

private:
    void Open();
    void Close();

    std::shared_ptr<HidBackend> backend_;
    HidInfo primary_hid_;
    HidBackend::Handle primary_handle_ = 0;
    std::map<std::string, HidBackend::Handle> endpoint_handles_;
    std::vector<EndpointDescriptor> endpoints_;
};
}

#endif // SIGNALBRIDGE_ENDPOINT_SESSION_H
