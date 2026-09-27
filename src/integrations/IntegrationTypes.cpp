#include "integrations/IntegrationTypes.h"

namespace rose::integrations
{
    std::string_view capabilityName(
        const IntegrationCapability capability) noexcept
    {
        switch (capability)
        {
        case IntegrationCapability::MailRead: return "mail.read";
        case IntegrationCapability::MailDraft: return "mail.draft";
        case IntegrationCapability::MailSend: return "mail.send";
        case IntegrationCapability::CalendarRead: return "calendar.read";
        case IntegrationCapability::CalendarWrite: return "calendar.write";
        case IntegrationCapability::WebAccess: return "web.access";
        }

        return "unknown";
    }

    std::optional<IntegrationCapability> parseCapability(
        const std::string_view text) noexcept
    {
        if (text == "mail.read") return IntegrationCapability::MailRead;
        if (text == "mail.draft") return IntegrationCapability::MailDraft;
        if (text == "mail.send") return IntegrationCapability::MailSend;
        if (text == "calendar.read") return IntegrationCapability::CalendarRead;
        if (text == "calendar.write") return IntegrationCapability::CalendarWrite;
        if (text == "web.access") return IntegrationCapability::WebAccess;
        return std::nullopt;
    }
}
