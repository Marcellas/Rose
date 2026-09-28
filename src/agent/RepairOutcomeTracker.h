#pragma once

#include "agent/SourceRepairPlan.h"
#include "tools/ToolTypes.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace rose::agent
{
    enum class RepairOutcomeStatus
    {
        None,
        PatchAppliedPendingValidation,
        PatchAppliedUnverified,
        ValidationSucceeded,
        ValidationFailed
    };


    // Compact Rose-owned correlation record for one diagnostic-driven source
    // repair. This is execution provenance, not user/project memory.
    //
    // Raw source and replacement text deliberately do not live here. The record
    // keeps only the source coordinates/digest, grounded diagnostic coordinates,
    // exact validation request (when available), and the resulting status.
    struct RepairOutcomeState
    {
        RepairOutcomeStatus status{ RepairOutcomeStatus::None };

        // Step index of the source mutation that created this repair outcome.
        // Journal events later reuse this value to correlate "patch applied" with
        // the validation result without requiring a separate persistent id store.
        std::size_t repairStepIndex{ 0 };

        std::string path;
        std::size_t startLine{ 0 };
        std::size_t lineCount{ 0 };
        std::string expectedDigest;
        std::optional<SourceRepairDiagnostic> diagnostic;
        std::optional<tools::ToolRequest> validationRequest;

        // True when a failed correlated validation contributed a new grounded
        // source diagnostic. This helps the next control pass distinguish a
        // repair that merely failed from one that can continue another repair
        // cycle with fresh source evidence.
        bool validationProducedGroundedDiagnostic{ false };
    };


    enum class RepairOutcomeTransition
    {
        None,
        PatchApplied,
        ValidationSucceeded,
        ValidationFailed
    };


    // Observe a completed provenance-bound edit. Only diagnostic-driven plans are
    // tracked here; ordinary/manual source edits remain ordinary tool operations.
    // A successful edit is explicitly "applied" but not "proven" until an exact
    // correlated validation request succeeds.
    [[nodiscard]]
    RepairOutcomeTransition observeAppliedRepairOutcome(
        RepairOutcomeState& state,
        const SourceRepairPlan& plan,
        std::size_t repairStepIndex,
        const tools::ToolResult& editResult,
        std::optional<tools::ToolRequest> validationRequest);


    // Observe configure/build/test completion against the exact validation request retained
    // for this repair. Unrelated validation commands do not change the outcome.
    [[nodiscard]]
    RepairOutcomeTransition observeRepairValidationOutcome(
        RepairOutcomeState& state,
        const tools::ToolRequest& request,
        const tools::ToolResult& result);


    [[nodiscard]]
    std::string_view repairOutcomeStatusName(
        RepairOutcomeStatus status) noexcept;


    // Compact Rose-owned control/final-response metadata. It never contains raw
    // compiler output, source text, or replacement text.
    [[nodiscard]]
    std::string formatRepairOutcomeMetadata(
        const RepairOutcomeState& state);


    // Bounded structured-ish journal detail shared by RepairApplied,
    // RepairValidated, and RepairValidationFailed events.
    [[nodiscard]]
    std::string formatRepairOutcomeJournalDetail(
        const RepairOutcomeState& state);

} // namespace rose::agent
