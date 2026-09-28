#pragma once

#include "agent/ToolCompletionAccumulator.h"
#include "agent/RepairValidationReplay.h"
#include "agent/RepairOutcomeTracker.h"
#include "agent/ToolConfirmation.h"
#include "agent/AgentJournal.h"
#include "artifacts/Artifact.h"
#include "tools/ToolTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rose::logging
{
    class Logger;
}

namespace rose::tools
{
    class ToolRegistry;
    struct ToolRequest;
}

namespace rose::agent
{
    class AgentJournal;
    class ToolExecutionService;
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
        // Stable id used to correlate all structured AgentJournal events across a
        // confirmation pause/resume boundary.
        std::uint64_t runId{ 0 };

        std::string originalUserText;
        std::string transientContext;

        // Latest Rose-owned structured metadata from a completed tool. This is
        // kept separate from ordinary transientContext so untrusted tool/file
        // output cannot spoof routing metadata by printing internal tag text.
        std::string latestTrustedToolMetadata{};

        // A grounded configure/build/test diagnostic survives the immediately following
        // matching source-window read so Rose can explain WHY a later patch is
        // being proposed. It is cleared by unrelated reads/actions and by any
        // mutation. Raw configure/compiler/test text never enters this trusted field.
        std::string latestDiagnosticMetadata{};

        // Exact source-window provenance from the latest completed range read.
        // This remains typed and ephemeral; source contents themselves stay in
        // transientContext as untrusted evidence. A later unrelated tool clears it.
        std::optional<tools::SourceWindowEvidence> latestSourceWindowEvidence{};

        // Exact failed configure/build/test request retained only long enough to validate a
        // provenance-bound source repair. The replay becomes eligible only after
        // that repair succeeds and still requires normal confirmation.
        RepairValidationReplayState repairValidationReplay{};

        // Correlates one diagnostic-driven patch with its exact post-repair
        // validation result. This state is ephemeral execution provenance only;
        // the persistent black-box trail is represented by journal events.
        RepairOutcomeState repairOutcome{};

        std::size_t executedToolCount{ 0 };

        // Canonical fingerprints of tool requests that have already executed in
        // this run. This prevents an imperfect model from repeatedly issuing the
        // exact same side effect until the step budget is exhausted. A tightly
        // scoped exception permits re-running the same CMake build after an
        // intervening confirmed local mutation so one repair cycle can validate.
        std::vector<std::string> executedRequestFingerprints;

        ToolCompletionAccumulator toolCompletion;
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

        std::optional<std::string> authoritativeResponse;

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
    //   AgentLoop borrows ToolSelectionAgent, ToolRegistry, ToolExecutionService,
    //   AgentJournal, and Logger. All remain worker-thread-owned and must outlive
    //   AgentLoop.
    class AgentLoop final
    {
    public:
        AgentLoop(
            ToolSelectionAgent& selectionAgent,
            tools::ToolRegistry& toolRegistry,
            ToolExecutionService& executionService,
            AgentJournal& journal,
            logging::Logger& logger,
            AgentLoopConfig config = {});

        [[nodiscard]]
        AgentLoopResult start(
            std::string userText,
            std::string initialTransientContext = {},
            AgentRunProvenance provenance = {});

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
        ToolExecutionService& executionService_;
        AgentJournal& journal_;
        logging::Logger& logger_;
        AgentLoopConfig config_;
    };

} // namespace rose::agent
