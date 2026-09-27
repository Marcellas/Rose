#include "integrations/IntegrationCommand.h"

#include <stdexcept>

namespace rose::integrations
{
    namespace
    {
        constexpr std::string_view commandPrefix{ "/integration " };

        IntegrationCommand parseMutation(
            const IntegrationCommandKind kind,
            const std::string_view remainder)
        {
            const std::size_t separator = remainder.find(' ');
            if (separator == std::string_view::npos
                || separator == 0
                || separator + 1 >= remainder.size())
            {
                throw std::invalid_argument{
                    "Usage: /integration allow <id> <capability> or /integration deny <id> <capability>."
                };
            }

            const std::string_view integrationId = remainder.substr(0, separator);
            const std::string_view capabilityText = remainder.substr(separator + 1);
            const auto capability = parseCapability(capabilityText);
            if (!capability.has_value())
            {
                throw std::invalid_argument{
                    "Unknown integration capability. Use mail.read, mail.draft, mail.send, calendar.read, calendar.write, or web.access."
                };
            }

            return IntegrationCommand{
                .kind = kind,
                .integrationId = std::string{ integrationId },
                .capability = *capability
            };
        }
    }

    std::optional<IntegrationCommand> parseIntegrationCommand(const std::string_view text)
    {
        if (text == "/integrations")
        {
            return IntegrationCommand{
                .kind = IntegrationCommandKind::List,
                .integrationId = {},
                .capability = IntegrationCapability::MailRead
            };
        }

        if (!text.starts_with(commandPrefix))
        {
            if (text == "/integration" || text.starts_with("/integrations "))
            {
                throw std::invalid_argument{
                    "Usage: /integrations, /integration allow <id> <capability>, or /integration deny <id> <capability>."
                };
            }
            return std::nullopt;
        }

        const std::string_view remainder = text.substr(commandPrefix.size());
        constexpr std::string_view allowPrefix{ "allow " };
        constexpr std::string_view denyPrefix{ "deny " };

        if (remainder.starts_with(allowPrefix))
        {
            return parseMutation(
                IntegrationCommandKind::Allow,
                remainder.substr(allowPrefix.size()));
        }
        if (remainder.starts_with(denyPrefix))
        {
            return parseMutation(
                IntegrationCommandKind::Deny,
                remainder.substr(denyPrefix.size()));
        }

        throw std::invalid_argument{
            "Usage: /integrations, /integration allow <id> <capability>, or /integration deny <id> <capability>."
        };
    }
}
