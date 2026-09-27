#pragma once

#include "integrations/ICredentialVault.h"
#include "integrations/OutlookClient.h"
#include "integrations/OutlookConfiguration.h"
#include "integrations/OutlookTypes.h"
#include "integrations/WinHttpClient.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <thread>
#include <mutex>
#include <string>

namespace rose::integrations
{
    // Owns all Outlook network activity on a dedicated thread. The SDL thread and
    // Rose's model/agent worker only enqueue tiny requests; neither blocks on OAuth
    // polling or Microsoft Graph network latency.
    class OutlookIntegrationWorker final
    {
    public:
        using EventCallback = std::function<void(OutlookEvent)>;

        OutlookIntegrationWorker(
            ICredentialVault& credentialVault,
            std::filesystem::path configurationPath,
            EventCallback onEvent);
        ~OutlookIntegrationWorker();

        OutlookIntegrationWorker(const OutlookIntegrationWorker&) = delete;
        OutlookIntegrationWorker& operator=(const OutlookIntegrationWorker&) = delete;

        void requestStatus();
        void configure(std::string clientId, std::string tenant = "common");
        void connect();
        void disconnect();
        void listInbox(std::size_t maximumMessages = 10);
        void search(std::string query, std::size_t maximumMessages = 10);

    private:
        enum class RequestKind { Status, Configure, Connect, Disconnect, Inbox, Search };
        struct Request
        {
            RequestKind kind{RequestKind::Status};
            std::string text;
            std::string tenant;
            std::size_t count{10};
        };

        void enqueue(Request request);
        void run(std::stop_token stopToken);
        void emit(OutlookEvent event) noexcept;

        EventCallback onEvent_;
        OutlookConfigurationStore configurationStore_;
        WinHttpClient httpClient_;
        OutlookClient client_;

        std::mutex mutex_;
        std::condition_variable_any requestAvailable_;
        std::deque<Request> requests_;
        std::jthread worker_;
    };
}
