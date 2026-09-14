#pragma once

#include "agent/ToolConfirmation.h"
#include "artifacts/Artifact.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rose::logging
{
    class Logger;
}

namespace rose::permissions
{
    class ToolExecutionPolicy;
}

namespace rose::tools
{
    class ToolRegistry;
    struct ToolRequest;
}

namespace rose::agent
{
    class ToolSelectionAgent;

    enum class AgentLoopStatus
    {
        ReadyForResponse,
        RequiresConfirmation
    };


    // Worker-owned, ephemeral state for one bounded agent run.
    //
    // This is deliberately NOT persistent memory. It only exists while Rose is
    // completing one user request (including a possible /confirm pause).
    struct AgentRunState
    {
        std::string originalUserText;
        std::string transientContext;

        std::size_t executedToolCount{ 0 };

        // Canonical fingerprints of tool requests that have already executed in
        // this run. This prevents an imperfect model from repeatedly issuing the
        // exact same side effect until the step budget is exhausted.
        std::vector<std::string> executedRequestFingerprints;
    };


    // A paused agent run owns BOTH:
    //   - the exact pending ToolRequest that needs confirmation, and
    //   - the already-completed observations needed to resume the same goal.
    //
    // /confirm never asks the model to reconstruct the request.
    struct PendingAgentRun
    {
        AgentRunState state;
        PendingToolConfirmation confirmation;
    };


    struct AgentLoopResult
    {
        AgentLoopStatus status{
            AgentLoopStatus::ReadyForResponse
        };

        // The canonical user text RoseCore should eventually commit when the
        // workflow reaches ReadyForResponse. On resume this is the ORIGINAL user
        // request, not the literal "/confirm" command.
        std::string userTextForResponse;

        // Trusted, non-persistent observations passed to RoseCore for the final
        // conversational response.
        std::string transientContext;

        // Newly created artifacts from THIS drive/resume call. main() may deliver
        // these immediately, even when the workflow pauses for confirmation.
        std::vector<artifacts::Artifact> artifacts;

        std::optional<PendingAgentRun> pending;

        std::size_t totalExecutedTools{ 0 };
        bool reachedToolLimit{ false };
    };


    struct AgentLoopConfig
    {
        // Small by design. This is not yet a general autonomous planner.
        std::size_t maximumToolExecutions{ 3 };
    };


    // AgentLoop coordinates repeated:
    //
    //   decide -> policy -> execute -> observe -> decide again
    //
    // until the model says RESPOND, a tool needs confirmation, or the hard tool
    // execution ceiling is reached.
    //
    // Ownership:
    //   AgentLoop borrows ToolSelectionAgent, ToolRegistry, ToolExecutionPolicy,
    //   and Logger. All remain worker-thread-owned and must outlive AgentLoop.
    class AgentLoop final
    {
    public:
        AgentLoop(
            ToolSelectionAgent& selectionAgent,
            tools::ToolRegistry& toolRegistry,
            permissions::ToolExecutionPolicy& executionPolicy,
            logging::Logger& logger,
            AgentLoopConfig config = {});

        [[nodiscard]]
        AgentLoopResult start(
            std::string userText,
            std::string initialTransientContext = {});

        // Resume after the user explicitly confirms the exact pending request.
        [[nodiscard]]
        AgentLoopResult resumeConfirmed(
            PendingAgentRun pending);

    private:
        [[nodiscard]]
        AgentLoopResult drive(
            AgentRunState state,
            std::optional<tools::ToolRequest> explicitlyConfirmedRequest);

        ToolSelectionAgent& selectionAgent_;
        tools::ToolRegistry& toolRegistry_;
        permissions::ToolExecutionPolicy& executionPolicy_;
        logging::Logger& logger_;
        AgentLoopConfig config_;
    };

} // namespace rose::agent
