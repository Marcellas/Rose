#include "integrations/WinHttpClient.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#endif

namespace rose::integrations
{
#if defined(_WIN32)
    namespace
    {
        std::wstring utf8ToWide(const std::string_view text)
        {
            if (text.empty()) return {};
            const int length = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                static_cast<int>(text.size()), nullptr, 0);
            if (length <= 0) throw std::runtime_error{ "WinHTTP UTF-8 conversion failed." };
            std::wstring result(static_cast<std::size_t>(length), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                    static_cast<int>(text.size()), result.data(), length) <= 0)
            {
                throw std::runtime_error{ "WinHTTP UTF-8 conversion failed." };
            }
            return result;
        }

        struct Handle
        {
            HINTERNET value{ nullptr };
            ~Handle() { if (value) WinHttpCloseHandle(value); }
            Handle() = default;
            explicit Handle(HINTERNET handle) : value{ handle } {}
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            Handle(Handle&& other) noexcept : value{ other.value } { other.value = nullptr; }
            Handle& operator=(Handle&& other) noexcept
            {
                if (this == &other) return *this;
                if (value) WinHttpCloseHandle(value);
                value = other.value;
                other.value = nullptr;
                return *this;
            }
        };

        [[noreturn]] void fail(const char* operation)
        {
            throw std::runtime_error{
                std::string{ operation } + " failed with Win32 error " + std::to_string(GetLastError()) + "."
            };
        }
    }
#endif

    bool WinHttpClient::available() const noexcept
    {
#if defined(_WIN32)
        return true;
#else
        return false;
#endif
    }

    HttpResponse WinHttpClient::send(const HttpRequest& request)
    {
#if defined(_WIN32)
        if (request.url.empty()) throw std::invalid_argument{ "HTTP request URL is empty." };

        const std::wstring wideUrl = utf8ToWide(request.url);
        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) fail("WinHttpCrackUrl");
        if (components.nScheme != INTERNET_SCHEME_HTTPS)
        {
            throw std::runtime_error{ "Rose integrations require HTTPS." };
        }

        const std::wstring host{ components.lpszHostName, components.dwHostNameLength };
        std::wstring path{ components.lpszUrlPath, components.dwUrlPathLength };
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
        if (path.empty()) path = L"/";

        Handle session{ WinHttpOpen(
            L"Rose/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
        if (!session.value) fail("WinHttpOpen");
        const int timeout = request.timeoutMilliseconds > 0
            ? request.timeoutMilliseconds : 15000;
        WinHttpSetTimeouts(session.value, 10000, 10000, timeout, timeout);

        Handle connection{ WinHttpConnect(session.value, host.c_str(), components.nPort, 0) };
        if (!connection.value) fail("WinHttpConnect");

        const std::wstring method = utf8ToWide(request.method);
        Handle requestHandle{ WinHttpOpenRequest(
            connection.value, method.c_str(), path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE) };
        if (!requestHandle.value) fail("WinHttpOpenRequest");

        for (const HttpHeader& header : request.headers)
        {
            const std::wstring line = utf8ToWide(header.name + ": " + header.value);
            if (!WinHttpAddRequestHeaders(
                    requestHandle.value, line.c_str(), static_cast<DWORD>(-1),
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            {
                fail("WinHttpAddRequestHeaders");
            }
        }

        if (request.body.size() > (std::numeric_limits<DWORD>::max)())
        {
            throw std::runtime_error{ "HTTP request body is too large." };
        }
        const DWORD bodySize = static_cast<DWORD>(request.body.size());
        LPVOID body = request.body.empty()
            ? WINHTTP_NO_REQUEST_DATA
            : const_cast<char*>(request.body.data());
        if (!WinHttpSendRequest(
                requestHandle.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                body, bodySize, bodySize, 0))
        {
            fail("WinHttpSendRequest");
        }
        if (!WinHttpReceiveResponse(requestHandle.value, nullptr)) fail("WinHttpReceiveResponse");

        DWORD status = 0;
        DWORD statusBytes = sizeof(status);
        if (!WinHttpQueryHeaders(
                requestHandle.value,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status, &statusBytes, WINHTTP_NO_HEADER_INDEX))
        {
            fail("WinHttpQueryHeaders");
        }

        std::string responseBody;
        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(requestHandle.value, &available)) fail("WinHttpQueryDataAvailable");
            if (available == 0) break;
            if (request.maximumResponseBytes != 0
                && available > request.maximumResponseBytes - responseBody.size())
                throw std::runtime_error{ "HTTP response exceeded the configured byte limit." };
            const std::size_t oldSize = responseBody.size();
            responseBody.resize(oldSize + available);
            DWORD read = 0;
            if (!WinHttpReadData(
                    requestHandle.value, responseBody.data() + oldSize,
                    available, &read))
            {
                fail("WinHttpReadData");
            }
            responseBody.resize(oldSize + read);
        }

        return HttpResponse{ static_cast<int>(status), std::move(responseBody) };
#else
        (void)request;
        throw std::runtime_error{ "WinHTTP is unavailable on this platform." };
#endif
    }
}
