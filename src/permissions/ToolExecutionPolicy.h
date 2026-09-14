#pragma once

#include "tools/ToolTypes.h"

#include <string>

namespace rose::permissions
{

    enum class ToolExecutionDisposition
    {
        Allowed,
        RequiresConfirmation,
        Denied
    };


    enum class ToolConfirmationState
    {
        NotConfirmed,
        ExplicitlyConfirmed
    };


    struct ToolExecutionDecision
    {
        ToolExecutionDisposition disposition{
            ToolExecutionDisposition::Denied
        };

        std::string reason;

        [[nodiscard]]
        bool allowed() const noexcept
        {
            return disposition
                == ToolExecutionDisposition::Allowed;
        }
    };


    // Central policy gate between an Agent proposal and actual tool execution.
    //
    // Explicit confirmation applies only to the exact ToolRequest Rose previously
    // placed in PendingToolConfirmation. The model cannot manufacture this state.
    class ToolExecutionPolicy final
    {
    public:
        [[nodiscard]]
        ToolExecutionDecision evaluate(
            const tools::ToolDescriptor& descriptor,
            ToolConfirmationState confirmation =
                ToolConfirmationState::NotConfirmed) const;
    };

} // namespace rose::permissions
