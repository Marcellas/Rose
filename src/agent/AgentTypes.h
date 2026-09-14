#pragma once

#include "tools/ToolTypes.h"

#include <optional>
#include <string>

namespace rose::agent
{

    // One control pass chooses either normal response or ONE next tool.
    // AgentLoop may call this selector repeatedly, but each individual decision
    // remains intentionally small and easy to validate.
    enum class AgentAction
    {
        RespondNormally,
        InvokeTool
    };


    struct AgentDecision
    {
        AgentAction action{ AgentAction::RespondNormally };
        std::optional<tools::ToolRequest> toolRequest;

        // Diagnostic only. Never shown to the user and never persisted as
        // conversation history.
        std::string rawModelOutput;
    };

} // namespace rose::agent
