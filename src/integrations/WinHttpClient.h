#pragma once

#include "integrations/HttpClient.h"

namespace rose::integrations
{
    // Small Windows-only HTTPS transport for optional integrations. Keeping HTTP
    // behind an interface makes provider logic testable without network access.
    class WinHttpClient final : public IHttpClient
    {
    public:
        [[nodiscard]] bool available() const noexcept override;
        [[nodiscard]] HttpResponse send(const HttpRequest& request) override;
    };
}
