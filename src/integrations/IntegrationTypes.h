#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rose::integrations
{
    // Local Rose-side capabilities are deliberately separate from provider OAuth
    // scopes. A future provider may need several remote scopes to implement one
    // Rose capability, but Rose still keeps an independent local allow/deny gate.
    enum class IntegrationCapability : std::uint8_t
    {
        MailRead = 1,
        MailDraft = 2,
        MailSend = 3,
        CalendarRead = 4,
        CalendarWrite = 5,
        WebAccess = 6
    };

    struct IntegrationPermissionRecord
    {
        std::string integrationId;
        std::vector<IntegrationCapability> allowedCapabilities;
        std::int64_t updatedUnixMilliseconds{ 0 };
    };

    struct IntegrationPermissionSnapshot
    {
        std::vector<IntegrationPermissionRecord> integrations;
    };

    [[nodiscard]] std::string_view capabilityName(
        IntegrationCapability capability) noexcept;

    [[nodiscard]] std::optional<IntegrationCapability> parseCapability(
        std::string_view text) noexcept;
}
