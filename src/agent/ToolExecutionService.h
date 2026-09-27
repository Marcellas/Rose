#pragma once

#include "permissions/ToolExecutionPolicy.h"
#include "tools/ToolTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace rose::agent
{
    class AgentJournal;
}

namespace rose::tools
{
    class ToolRegistry;
}

namespace rose::agent
{
    enum class ToolExecutionServiceStatus
    {
        Completed,
        RequiresConfirmation,
        Denied
    };

    struct ToolExecutionServiceResult
    {
        ToolExecutionServiceStatus status{
            ToolExecutionServiceStatus::Denied
        };
        tools::ToolDescriptor descriptor;
        tools::ToolResult result;
        std::string reason;
    };

    // Shared execution boundary for all Rose tool callers.
    //
    // Future UI commands, background jobs, project generators, and the model-driven
    // Agent can all reuse this service instead of duplicating permission checks and
    // audit events around ToolRegistry::execute().
    class ToolExecutionService final
    {
    public:
        ToolExecutionService(
            tools::ToolRegistry& registry,
            permissions::ToolExecutionPolicy& policy,
            AgentJournal& journal);

        [[nodiscard]]
        ToolExecutionServiceResult execute(
            const tools::ToolRequest& request,
            std::uint64_t runId,
            std::size_t stepIndex,
            permissions::ToolConfirmationState confirmation =
                permissions::ToolConfirmationState::NotConfirmed);

    private:
        tools::ToolRegistry& registry_;
        permissions::ToolExecutionPolicy& policy_;
        AgentJournal& journal_;
    };
}
