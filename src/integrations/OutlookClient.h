#pragma once

#include "integrations/HttpClient.h"
#include "integrations/ICredentialVault.h"
#include "integrations/OutlookConfiguration.h"
#include "integrations/OutlookTypes.h"

#include <chrono>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace rose::integrations
{
    class OutlookClient final
    {
    public:
        using DeviceCodeCallback = std::function<void(const OutlookEvent&)>;

        OutlookClient(
            IHttpClient& httpClient,
            ICredentialVault& credentialVault,
            OutlookConfigurationStore& configurationStore);

        [[nodiscard]] OutlookStatus status();
        void configure(OutlookConfiguration configuration);
        void disconnect();
        void connect(std::stop_token stopToken, const DeviceCodeCallback& onDeviceCode);
        [[nodiscard]] std::vector<OutlookMessageSummary> inbox(std::size_t maximumMessages);
        [[nodiscard]] std::vector<OutlookMessageSummary> search(std::string query, std::size_t maximumMessages);

    private:
        [[nodiscard]] OutlookConfiguration requireConfiguration();
        [[nodiscard]] std::string acquireAccessToken();
        [[nodiscard]] std::string refreshAccessToken(const OutlookConfiguration& configuration);
        [[nodiscard]] std::vector<OutlookMessageSummary> requestMessages(std::string url);
        void rememberTokenResponse(const std::string& responseBody);

        IHttpClient& httpClient_;
        ICredentialVault& credentialVault_;
        OutlookConfigurationStore& configurationStore_;
        std::string accessToken_;
        std::chrono::steady_clock::time_point accessTokenExpiresAt_{};

        static constexpr std::string_view refreshTokenKey{"integrations.outlook.refresh-token"};
    };
}
