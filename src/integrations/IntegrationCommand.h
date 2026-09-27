#pragma once

#include "integrations/IntegrationTypes.h"

#include <optional>
#include <string>
#include <string_view>

namespace rose::integrations
{
    enum class IntegrationCommandKind
    {
        List,
        Allow,
        Deny
    };

    struct IntegrationCommand
    {
        IntegrationCommandKind kind{ IntegrationCommandKind::List };
        std::string integrationId;
        IntegrationCapability capability{ IntegrationCapability::MailRead };
    };

    // Returns nullopt when the text is not an integration command. Recognized but
    // malformed commands throw invalid_argument with user-facing usage guidance.
    [[nodiscard]] std::optional<IntegrationCommand> parseIntegrationCommand(
        std::string_view text);
}
