#pragma once

#include "agent/SourceRepairPlan.h"
#include "tools/ToolTypes.h"

#include <optional>
#include <string>

namespace rose::agent
{
    // Ephemeral state for re-running the exact validation command that produced
    // the diagnostic behind a confirmed source repair.
    //
    // This is intentionally not persistent memory. The request is retained only
    // inside one AgentRunState so Rose can validate the specific repair without
    // asking the model to reconstruct configure/build/test arguments from text.
    struct RepairValidationReplayState
    {
        std::optional<tools::ToolRequest> failedValidationRequest;
        bool ready{ false };
    };


    [[nodiscard]]
    bool isDeveloperValidationRequest(
        const tools::ToolRequest& request) noexcept;


    // Observe the completed result of a configure/build/test validation tool. A failed validation with
    // a grounded source diagnostic becomes the candidate that a later repair may
    // replay. A successful validation clears stale replay state.
    void observeValidationResult(
        RepairValidationReplayState& state,
        const tools::ToolRequest& request,
        const tools::ToolResult& result);


    // A successful provenance-bound repair makes replay eligible only when its
    // trusted diagnostic came from the same validation tool as the remembered
    // failed request. This prevents unrelated edits from inheriting old builds.
    void observeCompletedRepair(
        RepairValidationReplayState& state,
        const SourceRepairPlan& plan,
        const tools::ToolResult& result);


    // Returns the exact retained validation request only after a qualifying repair.
    [[nodiscard]]
    std::optional<tools::ToolRequest> pendingRepairValidationRequest(
        const RepairValidationReplayState& state);


    // Compact Rose-owned control metadata. It contains request coordinates only,
    // never raw compiler output or source text.
    [[nodiscard]]
    std::string formatRepairValidationReplayMetadata(
        const RepairValidationReplayState& state);

} // namespace rose::agent
