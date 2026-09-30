#include "tools/SearchOnlineTool.h"

#include "integrations/HttpClient.h"
#include "integrations/SimpleJson.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace rose::tools
{
    namespace
    {
        std::string encodeQuery(std::string_view query)
        {
            constexpr char hex[] = "0123456789ABCDEF";
            std::string encoded;
            for (unsigned char c : query)
            {
                if ((std::isalnum(c) && c < 128) || c == '-' || c == '_' || c == '.' || c == '~')
                    encoded.push_back(static_cast<char>(c));
                else
                {
                    encoded.push_back('%');
                    encoded.push_back(hex[c >> 4]);
                    encoded.push_back(hex[c & 15]);
                }
            }
            return encoded;
        }

        std::string oneLine(std::string text, std::size_t maximum)
        {
            for (char& c : text) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
            if (text.size() > maximum) text.resize(maximum);
            return text;
        }
    }

    SearchOnlineTool::SearchOnlineTool(integrations::IHttpClient& http, std::string apiKey)
        : http_{ http }, apiKey_{ std::move(apiKey) }, descriptor_{
            .id = "search_online",
            .displayName = "Search Online",
            .description = "Search the public web using Brave Search API. Returns up to five URLs and snippets, not full pages. Sends the exact query to Brave. Requires BRAVE_SEARCH_API_KEY and one confirmation per query.",
            .risk = ToolRisk::ExternalEffect,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                { .name = "query", .description = "Search terms from the user's current request (up to 300 bytes).", .type = ToolValueType::String, .required = true }
            }
        }
    {
    }

    const ToolDescriptor& SearchOnlineTool::descriptor() const noexcept { return descriptor_; }

    ToolResult SearchOnlineTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id || request.arguments.size() != 1
            || !request.arguments.contains("query"))
            throw std::invalid_argument{ "search_online requires exactly one query." };

        const std::string& query = request.arguments.at("query");
        if (query.empty() || query.size() > 300 || query.find_first_of("\r\n") != std::string::npos)
            throw std::invalid_argument{ "Search query must be one line of 1..300 bytes." };
        if (apiKey_.empty())
            throw std::runtime_error{ "Online search is not configured. Set BRAVE_SEARCH_API_KEY and restart Rose." };
        if (!http_.available())
            throw std::runtime_error{ "Online search requires WinHTTP on Windows." };

        const integrations::HttpResponse response = http_.send({
            .method = "GET",
            .url = "https://api.search.brave.com/res/v1/web/search?q="
                + encodeQuery(query) + "&count=5",
            .headers = { { "Accept", "application/json" },
                { "X-Subscription-Token", apiKey_ } },
            .body = {},
            .maximumResponseBytes = 512u * 1024u,
            .timeoutMilliseconds = 8000
        });
        if (response.statusCode != 200)
            throw std::runtime_error{ "Online search failed (HTTP "
                + std::to_string(response.statusCode) + "). No results were obtained." };
        if (response.body.size() > 512u * 1024u)
            throw std::runtime_error{ "Online search response exceeded the size limit." };

        const auto root = integrations::json::parse(response.body);
        const auto* web = root.find("web");
        const auto* results = web ? web->find("results") : nullptr;
        const auto* array = results ? results->array() : nullptr;
        std::ostringstream text;
        text << "Online search for: " << query << "\n"
             << "Search results are snippets, not verified page contents.\n";
        std::size_t count = 0;
        if (array)
        {
            for (const auto& item : *array)
            {
                if (count == 5) break;
                const std::string url = integrations::json::stringOr(item, "url");
                if (!url.starts_with("https://") && !url.starts_with("http://")) continue;
                text << ++count << ". "
                     << oneLine(integrations::json::stringOr(item, "title"), 200)
                     << "\n" << oneLine(url, 2048) << "\n"
                     << oneLine(integrations::json::stringOr(item, "description"), 400)
                     << "\n";
            }
        }
        if (count == 0) text << "No web results returned.\n";
        return { .success = true, .message = text.str(),
            .trustedMetadata = {}, .sourceWindowEvidence = std::nullopt,
            .responseMode = ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {} };
    }
}
