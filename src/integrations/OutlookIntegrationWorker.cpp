#include "integrations/OutlookIntegrationWorker.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace rose::integrations
{
    OutlookIntegrationWorker::OutlookIntegrationWorker(
        ICredentialVault& credentialVault,
        std::filesystem::path configurationPath,
        EventCallback onEvent)
        : onEvent_{ std::move(onEvent) }
        , configurationStore_{ std::move(configurationPath) }
        , client_{ httpClient_, credentialVault, configurationStore_ }
        , worker_{ [this](std::stop_token stopToken) { run(stopToken); } }
    {
    }

    OutlookIntegrationWorker::~OutlookIntegrationWorker()
    {
        worker_.request_stop();
        requestAvailable_.notify_all();
    }

    void OutlookIntegrationWorker::requestStatus() { enqueue(Request{ .kind = RequestKind::Status, .text = {}, .tenant = {}, .count = 10 }); }
    void OutlookIntegrationWorker::configure(std::string clientId, std::string tenant)
    {
        enqueue(Request{ .kind = RequestKind::Configure, .text = std::move(clientId), .tenant = std::move(tenant) });
    }
    void OutlookIntegrationWorker::connect() { enqueue(Request{ .kind = RequestKind::Connect, .text = {}, .tenant = {}, .count = 10 }); }
    void OutlookIntegrationWorker::disconnect() { enqueue(Request{ .kind = RequestKind::Disconnect, .text = {}, .tenant = {}, .count = 10 }); }
    void OutlookIntegrationWorker::listInbox(const std::size_t maximumMessages)
    {
        enqueue(Request{ .kind = RequestKind::Inbox, .text = {}, .tenant = {}, .count = maximumMessages });
    }
    void OutlookIntegrationWorker::search(std::string query, const std::size_t maximumMessages)
    {
        enqueue(Request{ .kind = RequestKind::Search, .text = std::move(query), .tenant = {}, .count = maximumMessages });
    }

    void OutlookIntegrationWorker::enqueue(Request request)
    {
        {
            std::lock_guard lock{ mutex_ };
            requests_.push_back(std::move(request));
        }
        requestAvailable_.notify_one();
    }

    void OutlookIntegrationWorker::emit(OutlookEvent event) noexcept
    {
        try { if (onEvent_) onEvent_(std::move(event)); }
        catch (...) {}
    }

    void OutlookIntegrationWorker::run(const std::stop_token stopToken)
    {
        while (!stopToken.stop_requested())
        {
            Request request;
            {
                std::unique_lock lock{ mutex_ };
                requestAvailable_.wait(lock, stopToken, [this] { return !requests_.empty(); });
                if (stopToken.stop_requested()) return;
                request = std::move(requests_.front());
                requests_.pop_front();
            }

            try
            {
                switch (request.kind)
                {
                case RequestKind::Status:
                {
                    const OutlookStatus status = client_.status();
                    std::ostringstream text;
                    text << "Outlook status\n"
                         << "- HTTPS transport: " << (status.transportAvailable ? "ready" : "unavailable") << '\n'
                         << "- App configuration: " << (status.configured ? "configured" : "missing") << '\n'
                         << "- Tenant: " << (status.tenant.empty() ? "(not configured)" : status.tenant) << '\n'
                         << "- Secure refresh credential: " << (status.refreshCredentialPresent ? "present" : "not connected");
                    emit(OutlookEvent{ .type = OutlookEventType::Status, .text = text.str(), .verificationUri = {}, .userCode = {}, .messages = {} });
                    break;
                }
                case RequestKind::Configure:
                    client_.configure(OutlookConfiguration{ std::move(request.text), std::move(request.tenant) });
                    emit(OutlookEvent{
                        .type = OutlookEventType::Status,
                        .text = "Saved Rose's non-secret Outlook app configuration. Use /outlook connect to authorize an account.",
                        .verificationUri = {}, .userCode = {}, .messages = {}
                    });
                    break;

                case RequestKind::Connect:
                    client_.connect(stopToken, [this](const OutlookEvent& event) { emit(event); });
                    emit(OutlookEvent{
                        .type = OutlookEventType::Connected,
                        .text = "Outlook account connected. The refresh token is stored in Windows Credential Manager.",
                        .verificationUri = {}, .userCode = {}, .messages = {}
                    });
                    break;

                case RequestKind::Disconnect:
                    client_.disconnect();
                    emit(OutlookEvent{
                        .type = OutlookEventType::Disconnected,
                        .text = "Outlook account disconnected and Rose's stored refresh credential was removed.",
                        .verificationUri = {}, .userCode = {}, .messages = {}
                    });
                    break;

                case RequestKind::Inbox:
                    emit(OutlookEvent{
                        .type = OutlookEventType::Messages,
                        .text = "Outlook inbox",
                        .verificationUri = {}, .userCode = {},
                        .messages = client_.inbox(request.count)
                    });
                    break;

                case RequestKind::Search:
                    emit(OutlookEvent{
                        .type = OutlookEventType::Messages,
                        .text = "Outlook search: " + request.text,
                        .verificationUri = {}, .userCode = {},
                        .messages = client_.search(std::move(request.text), request.count)
                    });
                    break;
                }
            }
            catch (const std::exception& exception)
            {
                emit(OutlookEvent{ .type = OutlookEventType::Error, .text = std::string{ "Outlook: " } + exception.what(), .verificationUri = {}, .userCode = {}, .messages = {} });
            }
        }
    }
}
