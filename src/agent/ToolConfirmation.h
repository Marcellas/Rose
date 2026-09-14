#pragma once

#include "tools/ToolTypes.h"

#include <string>

namespace rose::agent
{

    // A confirmation is intentionally ephemeral. It exists only in Rose's
    // conversation worker and is never persisted across application restarts.
    //
    // The stored ToolRequest is the EXACT request that was previously proposed
    // and reviewed. /confirm executes this stored request rather than asking the
    // model to reconstruct it.
    struct PendingToolConfirmation
    {
        tools::ToolRequest request;
        std::string userFacingSummary;
    };


    [[nodiscard]]
    PendingToolConfirmation makePendingToolConfirmation(
        const tools::ToolRequest& request,
        const tools::ToolDescriptor& descriptor);


    // Trusted transient context for Rose's normal conversational response.
    // This block explicitly says the action has NOT been executed yet.
    [[nodiscard]]
    std::string buildToolConfirmationContext(
        const PendingToolConfirmation& pending);

} // namespace rose::agent
