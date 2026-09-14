#include "permissions/ToolExecutionPolicy.h"

namespace rose::permissions
{

    ToolExecutionDecision ToolExecutionPolicy::evaluate(
        const tools::ToolDescriptor& descriptor,
        const ToolConfirmationState confirmation) const
    {
        const bool explicitlyConfirmed =
            confirmation
            == ToolConfirmationState::ExplicitlyConfirmed;

        // Destructive and externally consequential tools are NEVER eligible for
        // silent automatic execution. They require confirmation even if a future
        // descriptor is accidentally marked AutoAllowed.
        if (
            descriptor.risk == tools::ToolRisk::Destructive
            || descriptor.risk == tools::ToolRisk::ExternalEffect)
        {
            if (explicitlyConfirmed)
            {
                return ToolExecutionDecision{
                    .disposition =
                        ToolExecutionDisposition::Allowed,

                    .reason =
                        "The user explicitly confirmed the exact pending tool request."
                };
            }

            return ToolExecutionDecision{
                .disposition =
                    ToolExecutionDisposition::RequiresConfirmation,

                .reason =
                    "Tool '"
                    + descriptor.id
                    + "' requires explicit confirmation before execution."
            };
        }

        if (
            descriptor.consent
            == tools::ToolConsent::AutoAllowed)
        {
            return ToolExecutionDecision{
                .disposition =
                    ToolExecutionDisposition::Allowed,

                .reason =
                    "Tool is permitted for automatic execution by current Rose policy."
            };
        }

        if (explicitlyConfirmed)
        {
            return ToolExecutionDecision{
                .disposition =
                    ToolExecutionDisposition::Allowed,

                .reason =
                    "The user explicitly confirmed the exact pending tool request."
            };
        }

        return ToolExecutionDecision{
            .disposition =
                ToolExecutionDisposition::RequiresConfirmation,

            .reason =
                "Tool '"
                + descriptor.id
                + "' requires explicit confirmation before execution."
        };
    }

} // namespace rose::permissions
