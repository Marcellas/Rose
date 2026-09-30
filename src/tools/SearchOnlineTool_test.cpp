#include "tools/SearchOnlineTool.h"
#include "integrations/HttpClient.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    class FakeHttp final : public rose::integrations::IHttpClient
    {
    public:
        rose::integrations::HttpRequest last;
        int calls{ 0 };
        bool enabled{ true };
        int status{ 200 };
        std::string body = R"({"web":{"results":[{"title":"Official site","url":"https://example.org/a","description":"A short snippet"},{"title":"Bad link","url":"file:///private","description":"ignored"}]}})";
        bool available() const noexcept override { return enabled; }
        rose::integrations::HttpResponse send(
            const rose::integrations::HttpRequest& request) override
        {
            ++calls;
            last = request;
            return { status, body };
        }
    };
}

int main()
{
    FakeHttp http;
    rose::tools::SearchOnlineTool tool{ http, "test-key" };
    assert(tool.descriptor().risk == rose::tools::ToolRisk::ExternalEffect);
    assert(tool.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation);
    const auto result = tool.execute({
        .toolId = "search_online", .arguments = { { "query", "C++ & SDL" } }
    });
    assert(http.calls == 1);
    assert(http.last.url.find("q=C%2B%2B%20%26%20SDL") != std::string::npos);
    assert(http.last.headers[1].value == "test-key");
    assert(http.last.maximumResponseBytes == 512u * 1024u);
    assert(http.last.timeoutMilliseconds == 8000);
    assert(result.message.find("https://example.org/a") != std::string::npos);
    assert(result.message.find("file:///private") == std::string::npos);
    assert(result.message.find("test-key") == std::string::npos);
    bool rejected = false;
    try { (void)tool.execute({ .toolId = "search_online",
        .arguments = { { "query", "x\nsecret" } } }); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected && http.calls == 1);
    http.body = R"({"web":{"results":[]}})";
    assert(tool.execute({ .toolId = "search_online",
        .arguments = { { "query", "no results" } } }).message.find(
            "No web results returned") != std::string::npos);
    http.status = 429;
    rejected = false;
    try { (void)tool.execute({ .toolId = "search_online",
        .arguments = { { "query", "limited" } } }); }
    catch (const std::runtime_error& e)
    { rejected = std::string{ e.what() }.find("HTTP 429") != std::string::npos; }
    assert(rejected);
    http.enabled = false;
    rejected = false;
    try { (void)tool.execute({ .toolId = "search_online",
        .arguments = { { "query", "offline" } } }); }
    catch (const std::runtime_error& e)
    { rejected = std::string{ e.what() }.find("WinHTTP") != std::string::npos; }
    assert(rejected);
    rose::tools::SearchOnlineTool unconfigured{ http, {} };
    rejected = false;
    try { (void)unconfigured.execute({ .toolId = "search_online",
        .arguments = { { "query", "missing key" } } }); }
    catch (const std::runtime_error& e)
    { rejected = std::string{ e.what() }.find("BRAVE_SEARCH_API_KEY") != std::string::npos; }
    assert(rejected);
    std::cout << "Rose online search tests: PASS\n";
}
