#pragma once

#include <string>
#include <cstddef>
#include <utility>
#include <vector>

namespace rose::integrations
{
    struct HttpHeader
    {
        std::string name;
        std::string value;
    };

    struct HttpRequest
    {
        std::string method{"GET"};
        std::string url;
        std::vector<HttpHeader> headers;
        std::string body;
        std::size_t maximumResponseBytes{ 0 }; // 0 uses the integration's default.
        int timeoutMilliseconds{ 0 }; // 0 uses the integration's default.
    };

    struct HttpResponse
    {
        int statusCode{0};
        std::string body;
    };

    class IHttpClient
    {
    public:
        virtual ~IHttpClient() = default;
        [[nodiscard]] virtual bool available() const noexcept = 0;
        [[nodiscard]] virtual HttpResponse send(const HttpRequest& request) = 0;
    };
}
