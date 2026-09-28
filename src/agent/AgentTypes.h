#pragma once

#include "agent/CodingTaskPlan.h"
#include "tools/ToolTypes.h"

#include <optional>
#include <string>

namespace rose::agent
{

    // One control pass chooses normal response, one bounded coding-plan update,
    // or ONE next tool. AgentLoop may call this selector repeatedly, but each
    // individual decision remains intentionally small and easy to validate.
    enum class AgentAction
    {
        RespondNormally,
        InvokeTool,
        PlanCodingTask
    };


    struct AgentDecision
    {
        AgentAction action{ AgentAction::RespondNormally };
        std::optional<tools::ToolRequest> toolRequest;

        // A bounded multi-file coding plan is advisory only. It cannot grant
        // filesystem/process authority and is never executed directly. AgentLoop
        // keeps it only for the lifetime of this bounded user request.
        std::optional<CodingTaskPlan> codingTaskPlan;

        // Diagnostic only. Never shown to the user and never persisted as
        // conversation history.
        std::string rawModelOutput;
    };

} // namespace rose::agent
