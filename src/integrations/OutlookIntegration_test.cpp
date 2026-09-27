#include "integrations/OutlookClient.h"
#include "integrations/OutlookCommand.h"
#include "integrations/SimpleJson.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace
{
    class FakeVault final : public rose::integrations::ICredentialVault
    {
    public:
        bool available() const noexcept override { return true; }
        std::string backendName() const override { return "fake"; }
        void storeSecret(std::string_view key, std::string_view secret) override
        {
            values[std::string{key}] = std::string{secret};
        }
        std::optional<std::string> readSecret(std::string_view key) const override
        {
            const auto it = values.find(std::string{key});
            if (it == values.end()) return std::nullopt;
            return it->second;
        }
        bool eraseSecret(std::string_view key) override
        {
            return values.erase(std::string{key}) != 0;
        }
        std::map<std::string, std::string> values;
    };

    class FakeHttp final : public rose::integrations::IHttpClient
    {
    public:
        bool available() const noexcept override { return true; }
        rose::integrations::HttpResponse send(const rose::integrations::HttpRequest& request) override
        {
            requests.push_back(request);
            assert(!responses.empty());
            auto response = responses.front();
            responses.erase(responses.begin());
            return response;
        }
        std::vector<rose::integrations::HttpRequest> requests;
        std::vector<rose::integrations::HttpResponse> responses;
    };
}

int main()
{
    namespace ri = rose::integrations;

    const auto json = ri::json::parse(R"({"name":"Rose","ok":true,"items":[1,2]})");
    assert(ri::json::stringOr(json, "name") == "Rose");
    assert(ri::json::boolOr(json, "ok", false));
    assert(json.find("items") && json.find("items")->array()->size() == 2);

    const auto command = ri::parseOutlookCommand("/outlook configure abc-123 organizations");
    assert(command && command->kind == ri::OutlookCommandKind::Configure);
    assert(command->text == "abc-123");
    assert(command->tenant == "organizations");

    bool tenantRequired = false;
    try
    {
        static_cast<void>(ri::parseOutlookCommand("/outlook configure abc-123"));
    }
    catch (const std::invalid_argument&)
    {
        tenantRequired = true;
    }
    assert(tenantRequired);
    const auto searchCommand = ri::parseOutlookCommand("/outlook search quarterly report");
    assert(searchCommand && searchCommand->text == "quarterly report");

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "rose-outlook-test.roseconfig";
    std::error_code error;
    std::filesystem::remove(path, error);

    ri::OutlookConfigurationStore store{path};
    FakeVault vault;
    FakeHttp http;
    ri::OutlookClient client{http, vault, store};
    client.configure(ri::OutlookConfiguration{"client-123", "common"});
    vault.storeSecret("integrations.outlook.refresh-token", "refresh-1");

    http.responses.push_back({200, R"({"access_token":"access-1","refresh_token":"refresh-2","expires_in":3600})"});
    http.responses.push_back({200, R"({"value":[{"id":"m1","subject":"Hello","receivedDateTime":"2026-09-17T20:00:00Z","isRead":false,"hasAttachments":true,"from":{"emailAddress":{"name":"Ada","address":"ada@example.com"}}}]})"});
    const auto inbox = client.inbox(5);
    assert(inbox.size() == 1);
    assert(inbox[0].subject == "Hello");
    assert(inbox[0].senderAddress == "ada@example.com");
    assert(inbox[0].hasAttachments);
    assert(vault.readSecret("integrations.outlook.refresh-token") == std::optional<std::string>{"refresh-2"});
    assert(http.requests.size() == 2);
    assert(http.requests[0].url.find("/token") != std::string::npos);
    assert(http.requests[1].url.find("mailFolders/inbox/messages") != std::string::npos);
    assert(http.requests[1].headers[0].value == "Bearer access-1");

    http.responses.push_back({200, R"({"value":[]})"});
    const auto results = client.search("pizza", 10);
    assert(results.empty());
    assert(http.requests.back().url.find("$search=%22pizza%22") != std::string::npos);

    client.configure(ri::OutlookConfiguration{"client-123", "common"});
    http.responses.push_back({400, R"({"error":"invalid_request","error_description":"AADSTS50059: No tenant-identifying information found."})"});
    bool tenantGuidance = false;
    try
    {
        client.connect(std::stop_token{}, {});
    }
    catch (const std::runtime_error& exception)
    {
        const std::string message = exception.what();
        tenantGuidance =
            message.find("Directory (tenant) ID") != std::string::npos
            && message.find("/outlook configure") != std::string::npos;
    }
    assert(tenantGuidance);

    client.disconnect();
    assert(!vault.readSecret("integrations.outlook.refresh-token").has_value());

    std::filesystem::remove(path, error);
    std::cout << "Rose OutlookIntegration tests: PASS\n";
    return 0;
}
