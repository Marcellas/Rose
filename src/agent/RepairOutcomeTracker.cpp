#include "agent/RepairOutcomeTracker.h"

#include <algorithm>
#include <sstream>
#include <string>

namespace rose::agent
{
    namespace
    {
        [[nodiscard]]
        bool requestsEqual(
            const tools::ToolRequest& left,
            const tools::ToolRequest& right)
        {
            return
                left.toolId == right.toolId
                && left.arguments == right.arguments;
        }


        [[nodiscard]]
        bool groundedDiagnosticResult(
            const tools::ToolResult& result) noexcept
        {
            return
                !result.success
                && isSourceDiagnosticMetadata(
                    result.trustedMetadata);
        }


        [[nodiscard]]
        std::string digestPrefix(
            const std::string& digest)
        {
            return digest.substr(
                0,
                (std::min)(std::size_t{ 12 }, digest.size()));
        }
    } // namespace


    RepairOutcomeTransition observeAppliedRepairOutcome(
        RepairOutcomeState& state,
        const SourceRepairPlan& plan,
        const std::size_t repairStepIndex,
        const tools::ToolResult& editResult,
        std::optional<tools::ToolRequest> validationRequest)
    {
        if (
            !editResult.success
            || !plan.diagnostic.has_value())
        {
            return RepairOutcomeTransition::None;
        }

        // The retained validation must come from the same developer-validation
        // capability that produced the grounded diagnostic. Future callers of
        // this helper therefore fail closed even if they accidentally provide an
        // unrelated request; such a patch is recorded as applied but unverified.
        if (
            validationRequest.has_value()
            && validationRequest->toolId
                != plan.diagnostic->producerTool)
        {
            validationRequest.reset();
        }

        state = RepairOutcomeState{
            .status =
                validationRequest.has_value()
                    ? RepairOutcomeStatus::PatchAppliedPendingValidation
                    : RepairOutcomeStatus::PatchAppliedUnverified,
            .repairStepIndex = repairStepIndex,
            .path = plan.path,
            .startLine = plan.startLine,
            .lineCount = plan.lineCount,
            .expectedDigest = plan.expectedDigest,
            .diagnostic = plan.diagnostic,
            .validationRequest = std::move(validationRequest),
            .validationProducedGroundedDiagnostic = false
        };

        return RepairOutcomeTransition::PatchApplied;
    }


    RepairOutcomeTransition observeRepairValidationOutcome(
        RepairOutcomeState& state,
        const tools::ToolRequest& request,
        const tools::ToolResult& result)
    {
        if (
            state.status
                != RepairOutcomeStatus::PatchAppliedPendingValidation
            || !state.validationRequest.has_value()
            || !requestsEqual(
                request,
                *state.validationRequest))
        {
            return RepairOutcomeTransition::None;
        }

        state.validationProducedGroundedDiagnostic =
            groundedDiagnosticResult(
                result);

        if (result.success)
        {
            state.status =
                RepairOutcomeStatus::ValidationSucceeded;

            return RepairOutcomeTransition::ValidationSucceeded;
        }

        state.status =
            RepairOutcomeStatus::ValidationFailed;

        return RepairOutcomeTransition::ValidationFailed;
    }


    std::string_view repairOutcomeStatusName(
        const RepairOutcomeStatus status) noexcept
    {
        switch (status)
        {
        case RepairOutcomeStatus::None:
            return "none";

        case RepairOutcomeStatus::PatchAppliedPendingValidation:
            return "patch_applied_pending_validation";

        case RepairOutcomeStatus::PatchAppliedUnverified:
            return "patch_applied_unverified";

        case RepairOutcomeStatus::ValidationSucceeded:
            return "validation_succeeded";

        case RepairOutcomeStatus::ValidationFailed:
            return "validation_failed";
        }

        return "unknown";
    }


    std::string formatRepairOutcomeMetadata(
        const RepairOutcomeState& state)
    {
        if (state.status == RepairOutcomeStatus::None)
        {
            return {};
        }

        std::ostringstream text;
        text
            << "<rose_repair_outcome>\n"
            << "trusted=true\n"
            << "status="
            << repairOutcomeStatusName(state.status)
            << "\n"
            << "repair_step="
            << state.repairStepIndex
            << "\n"
            << "path="
            << state.path
            << "\n"
            << "start_line="
            << state.startLine
            << "\n"
            << "line_count="
            << state.lineCount
            << "\n"
            << "source_digest_prefix="
            << digestPrefix(state.expectedDigest)
            << "\n";

        if (state.diagnostic.has_value())
        {
            text
                << "diagnostic_producer="
                << state.diagnostic->producerTool
                << "\n"
                << "diagnostic_line="
                << state.diagnostic->line
                << "\n"
                << "diagnostic_severity="
                << state.diagnostic->severity
                << "\n";

            if (!state.diagnostic->code.empty())
            {
                text
                    << "diagnostic_code="
                    << state.diagnostic->code
                    << "\n";
            }
        }

        if (state.validationRequest.has_value())
        {
            text
                << "validation_tool="
                << state.validationRequest->toolId
                << "\n";
        }

        text
            << "validation_produced_grounded_diagnostic="
            << (state.validationProducedGroundedDiagnostic ? "true" : "false")
            << "\n";

        switch (state.status)
        {
        case RepairOutcomeStatus::PatchAppliedPendingValidation:
            text
                << "The source patch was applied, but the repair is NOT yet proven successful. "
                   "The exact correlated validation still needs to run.\n";
            break;

        case RepairOutcomeStatus::PatchAppliedUnverified:
            text
                << "The source patch was applied, but no exact correlated validation request "
                   "is available. Do not describe the repair as proven successful.\n";
            break;

        case RepairOutcomeStatus::ValidationSucceeded:
            text
                << "The exact correlated post-repair validation succeeded. The tracked repair "
                   "is proven successful by that validation.\n";
            break;

        case RepairOutcomeStatus::ValidationFailed:
            text
                << "The exact correlated post-repair validation failed. The patch remains applied, "
                   "but the tracked repair is NOT proven successful.\n";
            break;

        case RepairOutcomeStatus::None:
            break;
        }

        text << "</rose_repair_outcome>";
        return text.str();
    }


    std::string formatRepairOutcomeJournalDetail(
        const RepairOutcomeState& state)
    {
        if (state.status == RepairOutcomeStatus::None)
        {
            return {};
        }

        const std::size_t endLine =
            state.lineCount == 0
                ? state.startLine
                : state.startLine + state.lineCount - 1u;

        std::ostringstream text;
        text
            << "repair_step="
            << state.repairStepIndex
            << "\n"
            << "status="
            << repairOutcomeStatusName(state.status)
            << "\n"
            << "path="
            << state.path
            << "\n"
            << "lines="
            << state.startLine;

        if (endLine != state.startLine)
        {
            text << "-" << endLine;
        }

        text << "\n";

        if (state.diagnostic.has_value())
        {
            text
                << "diagnostic_tool="
                << state.diagnostic->producerTool
                << "\n"
                << "diagnostic_line="
                << state.diagnostic->line
                << "\n";

            if (!state.diagnostic->code.empty())
            {
                text
                    << "diagnostic_code="
                    << state.diagnostic->code
                    << "\n";
            }
        }

        if (state.validationRequest.has_value())
        {
            text
                << "validation_tool="
                << state.validationRequest->toolId
                << "\n";
        }

        text
            << "validation_produced_grounded_diagnostic="
            << (state.validationProducedGroundedDiagnostic ? "true" : "false");

        return text.str();
    }

} // namespace rose::agent
