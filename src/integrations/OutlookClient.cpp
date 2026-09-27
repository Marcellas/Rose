#include "integrations/OutlookClient.h"

#include "integrations/SimpleJson.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace rose::integrations
{
    namespace
    {
        constexpr std::string_view scopes{ "offline_access Mail.ReadBasic" };

        bool waitForStop(const std::stop_token stopToken, const std::chrono::seconds duration)
        {
            std::mutex mutex;
            std::condition_variable_any condition;
            std::unique_lock lock{ mutex };
            condition.wait_for(lock, stopToken, duration, [] { return false; });
            return stopToken.stop_requested();
        }

        std::string urlEncode(const std::string_view text)
        {
            std::ostringstream output;
            output << std::uppercase << std::hex;
            for (const unsigned char c : text)
            {
                const bool unreserved =
                    (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
                if (unreserved) output << static_cast<char>(c);
                else output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            }
            return output.str();
        }

        std::string form(std::initializer_list<std::pair<std::string_view, std::string>> fields)
        {
            std::string result;
            bool first = true;
            for (const auto& [name, value] : fields)
            {
                if (!first) result += '&';
                first = false;
                result += name;
                result += '=';
                result += urlEncode(value);
            }
            return result;
        }

        std::string identityBase(const OutlookConfiguration& config)
        {
            return "https://login.microsoftonline.com/" + config.tenant + "/oauth2/v2.0/";
        }

        std::string providerErrorDetail(const HttpResponse& response)
        {
            std::string detail;
            try
            {
                const auto root = json::parse(response.body);
                detail = json::stringOr(root, "error_description");
                if (detail.empty()) detail = json::stringOr(root, "error");
            }
            catch (...) {}
            if (detail.empty()) detail = "HTTP " + std::to_string(response.statusCode);
            return detail;
        }

        [[noreturn]] void throwProviderError(const std::string& prefix, const HttpResponse& response)
        {
            throw std::runtime_error{ prefix + ": " + providerErrorDetail(response) };
        }

        OutlookMessageSummary parseMessage(const json::Value& value)
        {
            OutlookMessageSummary message;
            message.id = json::stringOr(value, "id");
            message.subject = json::stringOr(value, "subject", "(no subject)");
            message.receivedDateTime = json::stringOr(value, "receivedDateTime");
            message.isRead = json::boolOr(value, "isRead", false);
            message.hasAttachments = json::boolOr(value, "hasAttachments", false);
            if (const auto* from = value.find("from"))
            {
                if (const auto* address = from->find("emailAddress"))
                {
                    message.senderName = json::stringOr(*address, "name");
                    message.senderAddress = json::stringOr(*address, "address");
                }
            }
            return message;
        }
    }

    OutlookClient::OutlookClient(
        IHttpClient& httpClient,
        ICredentialVault& credentialVault,
        OutlookConfigurationStore& configurationStore)
        : httpClient_{ httpClient }
        , credentialVault_{ credentialVault }
        , configurationStore_{ configurationStore }
    {
    }

    OutlookStatus OutlookClient::status()
    {
        OutlookStatus result;
        result.transportAvailable = httpClient_.available();
        const OutlookConfiguration config = configurationStore_.load();
        result.configured = !config.clientId.empty();
        result.tenant = config.tenant;
        if (credentialVault_.available())
            result.refreshCredentialPresent = credentialVault_.readSecret(refreshTokenKey).has_value();
        return result;
    }

    void OutlookClient::configure(OutlookConfiguration configuration)
    {
        if (configuration.tenant.empty())
            throw std::invalid_argument{
                "Outlook tenant is required. Use /outlook configure <Azure-client-id> <Directory-tenant-ID>."
            };
        configurationStore_.save(configuration);
        accessToken_.clear();
        accessTokenExpiresAt_ = {};
    }

    void OutlookClient::disconnect()
    {
        accessToken_.clear();
        accessTokenExpiresAt_ = {};
        if (credentialVault_.available())
            static_cast<void>(credentialVault_.eraseSecret(refreshTokenKey));
    }

    OutlookConfiguration OutlookClient::requireConfiguration()
    {
        if (!httpClient_.available()) throw std::runtime_error{ "Outlook integration HTTP transport is unavailable." };
        if (!credentialVault_.available()) throw std::runtime_error{ "Secure credential vault is unavailable." };
        OutlookConfiguration config = configurationStore_.load();
        if (config.clientId.empty() || config.tenant.empty())
            throw std::runtime_error{
                "Outlook is not configured. Use /outlook configure <Azure-client-id> <Directory-tenant-ID>."
            };
        return config;
    }

    void OutlookClient::connect(const std::stop_token stopToken, const DeviceCodeCallback& onDeviceCode)
    {
        const OutlookConfiguration config = requireConfiguration();
        const HttpResponse device = httpClient_.send(HttpRequest{
            .method = "POST",
            .url = identityBase(config) + "devicecode",
            .headers = {{"Content-Type", "application/x-www-form-urlencoded"}},
            .body = form({{"client_id", config.clientId}, {"scope", std::string{ scopes }}})
        });
        if (device.statusCode != 200)
        {
            const std::string detail = providerErrorDetail(device);
            if (detail.find("AADSTS50059") != std::string::npos
                || detail.find("AADSTS90133") != std::string::npos)
            {
                throw std::runtime_error{
                    "Outlook device authorization failed for tenant authority '"
                    + config.tenant
                    + "'. Microsoft requires a tenant-specific authority for this app/flow. "
                      "Reconfigure Rose with the Directory (tenant) ID from the app registration: "
                      "/outlook configure <Azure-client-id> <Directory-tenant-ID>. Provider: "
                    + detail
                };
            }
            throw std::runtime_error{ "Outlook device authorization failed: " + detail };
        }

        const auto root = json::parse(device.body);
        const std::string deviceCode = json::stringOr(root, "device_code");
        const std::string userCode = json::stringOr(root, "user_code");
        const std::string verificationUri = json::stringOr(root, "verification_uri");
        const std::string message = json::stringOr(root, "message");
        const auto expiresSeconds = std::max<std::int64_t>(60, json::integerOr(root, "expires_in", 900));
        auto intervalSeconds = std::max<std::int64_t>(1, json::integerOr(root, "interval", 5));
        if (deviceCode.empty() || userCode.empty() || verificationUri.empty())
            throw std::runtime_error{ "Microsoft device authorization returned an incomplete response." };

        if (onDeviceCode)
        {
            onDeviceCode(OutlookEvent{
                .type = OutlookEventType::DeviceCode,
                .text = message,
                .verificationUri = verificationUri,
                .userCode = userCode,
                .messages = {}
            });
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ expiresSeconds };
        while (!stopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
        {
            if (waitForStop(stopToken, std::chrono::seconds{ intervalSeconds }))
                throw std::runtime_error{ "Outlook connection was cancelled." };
            const HttpResponse token = httpClient_.send(HttpRequest{
                .method = "POST",
                .url = identityBase(config) + "token",
                .headers = {{"Content-Type", "application/x-www-form-urlencoded"}},
                .body = form({
                    {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                    {"client_id", config.clientId},
                    {"device_code", deviceCode}
                })
            });
            if (token.statusCode == 200)
            {
                rememberTokenResponse(token.body);
                return;
            }

            std::string error;
            try { error = json::stringOr(json::parse(token.body), "error"); }
            catch (...) {}
            if (error == "authorization_pending") continue;
            if (error == "slow_down")
            {
                intervalSeconds += 5;
                continue;
            }
            throwProviderError("Outlook sign-in failed", token);
        }
        throw std::runtime_error{ "Outlook device authorization expired before sign-in completed." };
    }

    std::string OutlookClient::acquireAccessToken()
    {
        if (!accessToken_.empty()
            && std::chrono::steady_clock::now() + std::chrono::seconds{ 30 } < accessTokenExpiresAt_)
            return accessToken_;
        return refreshAccessToken(requireConfiguration());
    }

    std::string OutlookClient::refreshAccessToken(const OutlookConfiguration& config)
    {
        const auto refresh = credentialVault_.readSecret(refreshTokenKey);
        if (!refresh || refresh->empty())
            throw std::runtime_error{ "Outlook is not connected. Use /outlook connect." };
        const HttpResponse token = httpClient_.send(HttpRequest{
            .method = "POST",
            .url = identityBase(config) + "token",
            .headers = {{"Content-Type", "application/x-www-form-urlencoded"}},
            .body = form({
                {"grant_type", "refresh_token"},
                {"client_id", config.clientId},
                {"refresh_token", *refresh},
                {"scope", std::string{ scopes }}
            })
        });
        if (token.statusCode != 200)
        {
            if (token.statusCode == 400)
                static_cast<void>(credentialVault_.eraseSecret(refreshTokenKey));
            throwProviderError("Outlook token refresh failed; reconnect may be required", token);
        }
        rememberTokenResponse(token.body);
        return accessToken_;
    }

    void OutlookClient::rememberTokenResponse(const std::string& responseBody)
    {
        const auto root = json::parse(responseBody);
        const std::string access = json::stringOr(root, "access_token");
        const std::string refresh = json::stringOr(root, "refresh_token");
        const auto expires = std::max<std::int64_t>(60, json::integerOr(root, "expires_in", 3600));
        if (access.empty()) throw std::runtime_error{ "Microsoft token response did not contain an access token." };
        accessToken_ = access;
        accessTokenExpiresAt_ = std::chrono::steady_clock::now() + std::chrono::seconds{ expires };
        if (!refresh.empty()) credentialVault_.storeSecret(refreshTokenKey, refresh);
    }

    std::vector<OutlookMessageSummary> OutlookClient::inbox(const std::size_t maximumMessages)
    {
        const std::size_t top = std::clamp<std::size_t>(maximumMessages, 1, 50);
        return requestMessages(
            "https://graph.microsoft.com/v1.0/me/mailFolders/inbox/messages?"
            "$top=" + std::to_string(top)
            + "&$select=id,subject,from,receivedDateTime,isRead,hasAttachments"
            + "&$orderby=receivedDateTime%20desc");
    }

    std::vector<OutlookMessageSummary> OutlookClient::search(std::string query, const std::size_t maximumMessages)
    {
        if (query.empty()) throw std::invalid_argument{ "Outlook search query cannot be empty." };
        const std::size_t top = std::clamp<std::size_t>(maximumMessages, 1, 50);
        const std::string searchValue = "\"" + query + "\"";
        return requestMessages(
            "https://graph.microsoft.com/v1.0/me/messages?"
            "$search=" + urlEncode(searchValue)
            + "&$top=" + std::to_string(top)
            + "&$select=id,subject,from,receivedDateTime,isRead,hasAttachments");
    }

    std::vector<OutlookMessageSummary> OutlookClient::requestMessages(std::string url)
    {
        auto perform = [&](const std::string& token)
        {
            return httpClient_.send(HttpRequest{
                .method = "GET",
                .url = url,
                .headers = {
                    {"Authorization", "Bearer " + token},
                    {"Accept", "application/json"}
                },
                .body = {}
            });
        };

        HttpResponse response = perform(acquireAccessToken());
        if (response.statusCode == 401)
        {
            accessToken_.clear();
            accessTokenExpiresAt_ = {};
            response = perform(refreshAccessToken(requireConfiguration()));
        }
        if (response.statusCode != 200) throwProviderError("Microsoft Graph mail request failed", response);

        const auto root = json::parse(response.body);
        const auto* value = root.find("value");
        if (!value || !value->array()) throw std::runtime_error{ "Microsoft Graph mail response did not contain a message array." };
        std::vector<OutlookMessageSummary> messages;
        messages.reserve(value->array()->size());
        for (const auto& item : *value->array())
            if (item.object()) messages.push_back(parseMessage(item));
        return messages;
    }
}
