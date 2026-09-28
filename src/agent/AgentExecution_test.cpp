#include "agent/AgentJournal.h"
#include "agent/FileAgentJournalStore.h"
#include "agent/ToolExecutionService.h"
#include "agent/ToolConfirmation.h"
#include "agent/SourceWindowBinding.h"
#include "agent/SourceRepairPlan.h"
#include "agent/RepairValidationReplay.h"
#include "agent/RepairOutcomeTracker.h"
#include "files/SourceWindowDigest.h"
#include "permissions/ToolExecutionPolicy.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    void require(
        const bool condition,
        const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    class CountingTool final
        : public rose::tools::ITool
    {
    public:
        CountingTool(
            std::string id,
            const rose::tools::ToolRisk risk,
            const rose::tools::ToolConsent consent,
            int& executions)
            : executions_{ executions }
        {
            descriptor_.id = std::move(id);
            descriptor_.displayName = descriptor_.id;
            descriptor_.description = "Agent execution test tool.";
            descriptor_.risk = risk;
            descriptor_.consent = consent;
        }

        [[nodiscard]]
        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }

        [[nodiscard]]
        rose::tools::ToolResult execute(
            const rose::tools::ToolRequest&) override
        {
            ++executions_;
            return rose::tools::ToolResult{
                .success = true,
                .message = "counted",
                .responseMode = rose::tools::ToolResponseMode::RequiresModelSynthesis,
                .artifacts = {}
            };
        }

    private:
        rose::tools::ToolDescriptor descriptor_;
        int& executions_;
    };
}

int main()
{
    try
    {
        const auto root =
            std::filesystem::temp_directory_path()
            / "rose_agent_execution_test";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        error.clear();
        std::filesystem::create_directories(root, error);
        require(!error, "Could not create AgentExecution test directory.");

        const auto journalPath = root / "agent.rosejournal";
        int readExecutions{ 0 };
        int writeExecutions{ 0 };

        {
            rose::tools::ToolRegistry registry;
            registry.registerTool(
                std::make_unique<CountingTool>(
                    "inspect_local",
                    rose::tools::ToolRisk::ReadOnly,
                    rose::tools::ToolConsent::AutoAllowed,
                    readExecutions));
            registry.registerTool(
                std::make_unique<CountingTool>(
                    "write_local",
                    rose::tools::ToolRisk::LocalWrite,
                    rose::tools::ToolConsent::RequiresConfirmation,
                    writeExecutions));

            // Registration has an ownership-safe inverse. A dynamically loaded
            // plugin can remove an adapter without leaking or deleting it behind
            // the caller's back.
            auto removed = registry.removeTool("inspect_local");
            require(removed != nullptr, "removeTool should return ownership.");
            require(registry.find("inspect_local") == nullptr,
                    "Removed tool should no longer be registered.");
            registry.registerTool(std::move(removed));

            rose::agent::FileAgentJournalStore store{ journalPath };
            rose::agent::AgentJournal journal{
                rose::agent::AgentJournalConfig{
                    .maximumEvents = 32,
                    .maximumMessageBytes = 256,
                    .maximumDetailBytes = 512,
                    .maximumArgumentValueBytes = 128
                },
                &store
            };
            rose::permissions::ToolExecutionPolicy policy;
            rose::agent::ToolExecutionService executor{
                registry,
                policy,
                journal
            };

            const std::uint64_t runId = journal.beginRun(
                "Inspect then write.",
                rose::agent::AgentRunProvenance{
                    .projectId = "project-rose",
                    .discussionId = "discussion-b26"
                });

            const auto readResult = executor.execute(
                rose::tools::ToolRequest{
                    .toolId = "inspect_local",
                    .arguments = { { "path", "C:/Rose" } }
                },
                runId,
                1);
            require(
                readResult.status
                    == rose::agent::ToolExecutionServiceStatus::Completed,
                "Auto-allowed read tool should complete.");
            require(readExecutions == 1, "Read tool execution count mismatch.");

            const rose::tools::ToolRequest writeRequest{
                .toolId = "write_local",
                .arguments = { { "path", "C:/Rose/test.txt" } }
            };
            const auto pending = executor.execute(
                writeRequest,
                runId,
                2);
            require(
                pending.status
                    == rose::agent::ToolExecutionServiceStatus::RequiresConfirmation,
                "Local write should pause for confirmation.");
            require(writeExecutions == 0,
                    "Confirmation-gated tool must not execute early.");

            const auto confirmed = executor.execute(
                writeRequest,
                runId,
                2,
                rose::permissions::ToolConfirmationState::ExplicitlyConfirmed);
            require(
                confirmed.status
                    == rose::agent::ToolExecutionServiceStatus::Completed,
                "Confirmed local write should complete.");
            require(writeExecutions == 1, "Confirmed write execution count mismatch.");

            // Batch 47 appends repair-outcome event ids rather than inserting
            // them into the persisted enum. Writing one before RunCompleted makes
            // the restart check below exercise the durable store's accepted type
            // range while the run provenance is still active.
            journal.record(
                rose::agent::AgentEvent{
                    .runId = runId,
                    .type = rose::agent::AgentEventType::RepairValidated,
                    .stepIndex = 2,
                    .toolId = "build_cmake_project",
                    .projectId = {},
                    .discussionId = {},
                    .message = "repair validation test event",
                    .detail = "repair_step=1",
                    .duration = {}
                });

            journal.record(
                rose::agent::AgentEvent{
                    .runId = runId,
                    .type = rose::agent::AgentEventType::RunCompleted,
                    .stepIndex = 0,
                    .toolId = {},
                    .projectId = {},
                    .discussionId = {},
                    .message = "test complete",
                    .detail = {},
                    .duration = {}
                });

            const auto events = journal.snapshot();
            require(!events.empty(), "Agent journal should contain events.");
            for (const auto& event : events)
            {
                if (event.runId == runId)
                {
                    require(event.projectId == "project-rose",
                            "Run events should inherit project provenance.");
                    require(event.discussionId == "discussion-b26",
                            "Run events should inherit discussion provenance.");
                }
            }
        }

        // Durable black-box trail should survive a normal restart.
        {
            rose::agent::FileAgentJournalStore store{ journalPath };
            rose::agent::AgentJournal journal{
                rose::agent::AgentJournalConfig{
                    .maximumEvents = 32,
                    .maximumMessageBytes = 256,
                    .maximumDetailBytes = 512,
                    .maximumArgumentValueBytes = 128
                },
                &store
            };
            require(journal.size() >= 7,
                    "Persisted agent journal did not reload expected events.");
            require(journal.persistenceError().empty(),
                    "Persisted journal should reload without an error.");
            require(
                journal.formatRecent().find("project=project-rose")
                    != std::string::npos,
                "Formatted journal should expose project provenance.");
        }

        // Source-window provenance is bound only to the path and contiguous
        // line range Rose actually observed. Per-line hashes let a narrow patch
        // bind to a subrange without retaining raw source bytes in trusted state.
        const std::string observedLogical =
            "line 40\nline 41\nline 42\nline 43";
        const std::vector<std::string> observedLineDigests =
            rose::files::sourceLineSha256s(observedLogical);

        const rose::tools::SourceWindowEvidence evidence{
            .path = "C:/Rose/src/sample.cpp",
            .startLine = 40,
            .lineCount = 4,
            .lineSha256s = observedLineDigests,
            .sha256 = rose::files::sourceWindowSha256FromLineDigests(
                observedLineDigests)
        };

        const rose::tools::ToolRequest proposedPatch{
            .toolId = "edit_text_file",
            .arguments = {
                { "path", "C:/Rose/src/sample.cpp" },
                { "operation", "replace_line_range" },
                { "start_line", "41" },
                { "line_count", "2" },
                { "expected_text", "line 41\nline 42" },
                { "replacement_text", "new one\nnew two" }
            }
        };

        const rose::tools::ToolRequest boundPatch =
            rose::agent::bindSourceWindowEvidence(
                proposedPatch,
                evidence);

        require(
            !boundPatch.arguments.contains("expected_text")
                && boundPatch.arguments.at("expected_digest")
                    == rose::files::sourceWindowSha256("line 41\nline 42"),
            "observed source-window provenance should bind a contiguous subrange without model-echoed preimage text");

        rose::tools::ToolRequest outsideRange = proposedPatch;
        outsideRange.arguments["start_line"] = "43";
        const rose::tools::ToolRequest unboundRange =
            rose::agent::bindSourceWindowEvidence(
                outsideRange,
                evidence);
        require(
            unboundRange.arguments.contains("expected_text")
                && !unboundRange.arguments.contains("expected_digest"),
            "source-window provenance must not bind to lines outside the observed window");

        rose::tools::ToolRequest differentPath = proposedPatch;
        differentPath.arguments["path"] = "C:/Rose/src/other.cpp";
        const rose::tools::ToolRequest unboundPath =
            rose::agent::bindSourceWindowEvidence(
                differentPath,
                evidence);
        require(
            unboundPath.arguments.contains("expected_text")
                && !unboundPath.arguments.contains("expected_digest"),
            "source-window provenance must not bind to a different file");

        const std::string diagnosticMetadata =
            "metadata_kind=source_diagnostic\n"
            "producer_tool=build_cmake_project\n"
            "operation_success=false\n"
            "diagnostic_path=C:/Rose/src/sample.cpp\n"
            "diagnostic_line=42\n"
            "diagnostic_column=17\n"
            "diagnostic_severity=error\n"
            "diagnostic_code=C2065\n"
            "suggested_read_start_line=12\n"
            "suggested_read_line_count=80";

        require(
            rose::agent::sourceWindowCoversDiagnostic(
                evidence,
                diagnosticMetadata),
            "matching grounded diagnostic should remain attributable through the source-window read");

        const std::optional<rose::agent::SourceRepairPlan> repairPlan =
            rose::agent::buildSourceRepairPlan(
                boundPatch,
                evidence,
                diagnosticMetadata);

        require(
            repairPlan.has_value(),
            "a provenance-bound line patch should produce an explicit repair plan");
        require(
            repairPlan->diagnostic.has_value()
                && repairPlan->diagnostic->line == 42
                && repairPlan->diagnostic->code == "C2065",
            "repair plan should retain the grounded diagnostic that motivated the observed window");

        const std::string repairSummary =
            rose::agent::formatSourceRepairPlan(*repairPlan);
        require(
            repairSummary.find("Source repair plan:") != std::string::npos
                && repairSummary.find("lines: 41-42") != std::string::npos
                && repairSummary.find("build_cmake_project reported error C2065 at line 42, column 17")
                    != std::string::npos
                && repairSummary.find("41|new one") != std::string::npos
                && repairSummary.find("42|new two") != std::string::npos,
            "repair confirmation summary should explain what will change and why before mutation");

        const rose::tools::ToolDescriptor editDescriptor{
            .id = "edit_text_file",
            .displayName = "Edit Text File",
            .description = "test",
            .risk = rose::tools::ToolRisk::LocalWrite,
            .consent = rose::tools::ToolConsent::RequiresConfirmation,
            .parameters = {
                rose::tools::ToolParameterDescriptor{
                    .name = "path", .description = "",
                    .type = rose::tools::ToolValueType::String, .required = true },
                rose::tools::ToolParameterDescriptor{
                    .name = "operation", .description = "",
                    .type = rose::tools::ToolValueType::String, .required = true },
                rose::tools::ToolParameterDescriptor{
                    .name = "start_line", .description = "",
                    .type = rose::tools::ToolValueType::Integer, .required = false },
                rose::tools::ToolParameterDescriptor{
                    .name = "line_count", .description = "",
                    .type = rose::tools::ToolValueType::Integer, .required = false },
                rose::tools::ToolParameterDescriptor{
                    .name = "replacement_text", .description = "",
                    .type = rose::tools::ToolValueType::String, .required = false },
                rose::tools::ToolParameterDescriptor{
                    .name = "expected_digest", .description = "",
                    .type = rose::tools::ToolValueType::String, .required = false }
            }
        };

        const rose::agent::PendingToolConfirmation repairConfirmation =
            rose::agent::makePendingToolConfirmation(
                boundPatch,
                editDescriptor,
                repairPlan);
        require(
            repairConfirmation.userFacingSummary.find("Source repair plan:")
                    != std::string::npos
                && repairConfirmation.userFacingSummary.find("replacement preview:")
                    != std::string::npos
                && repairConfirmation.userFacingSummary.find("- expected_digest:")
                    == std::string::npos
                && repairConfirmation.userFacingSummary.find("Type /confirm")
                    != std::string::npos,
            "pending edit confirmation should surface the reviewable repair plan instead of an opaque digest");

        const std::string unrelatedDiagnostic =
            "metadata_kind=source_diagnostic\n"
            "producer_tool=run_cmake_tests\n"
            "operation_success=false\n"
            "diagnostic_path=C:/Rose/src/other.cpp\n"
            "diagnostic_line=42\n"
            "diagnostic_column=0\n"
            "diagnostic_severity=test_failure\n"
            "diagnostic_code=";
        require(
            !rose::agent::sourceWindowCoversDiagnostic(
                evidence,
                unrelatedDiagnostic),
            "diagnostic provenance must not survive an unrelated source-window read");

        // Batch 46: a grounded repair can replay the exact failed configure/build/test
        // request for validation without model reconstruction. Replay is dormant
        // until the provenance-bound repair itself succeeds.
        rose::agent::RepairValidationReplayState replayState;
        require(
            rose::agent::isDeveloperValidationRequest(
                rose::tools::ToolRequest{
                    .toolId = "reconfigure_cmake_project",
                    .arguments = { { "source_path", "C:/Rose" } }
                }),
            "CMake reconfiguration must participate in the same provenance-bound developer validation/replay boundary as builds and tests");

        const rose::tools::ToolRequest failedBuildRequest{
            .toolId = "build_cmake_project",
            .arguments = {
                { "source_path", "C:/Rose" },
                { "configuration", "Debug" },
                { "target", "Rose" },
                { "jobs", "8" }
            }
        };
        const rose::tools::ToolResult failedBuildResult{
            .success = false,
            .message = "compiler failed",
            .trustedMetadata = diagnosticMetadata,
            .responseMode = rose::tools::ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };

        rose::agent::observeValidationResult(
            replayState,
            failedBuildRequest,
            failedBuildResult);
        require(
            replayState.failedValidationRequest.has_value()
                && !replayState.ready
                && !rose::agent::pendingRepairValidationRequest(replayState).has_value(),
            "failed grounded validation should be remembered but not replayable before a repair");

        const rose::tools::ToolResult successfulEditResult{
            .success = true,
            .message = "patched",
            .responseMode = rose::tools::ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };
        rose::agent::observeCompletedRepair(
            replayState,
            *repairPlan,
            successfulEditResult);

        const auto replayRequest =
            rose::agent::pendingRepairValidationRequest(replayState);
        require(
            replayRequest.has_value()
                && replayRequest->toolId == failedBuildRequest.toolId
                && replayRequest->arguments == failedBuildRequest.arguments,
            "successful diagnostic-bound repair should replay the exact failed validation request");

        const std::string replayMetadata =
            rose::agent::formatRepairValidationReplayMetadata(replayState);
        require(
            replayMetadata.find("status=ready") != std::string::npos
                && replayMetadata.find("tool_id=build_cmake_project") != std::string::npos
                && replayMetadata.find("argument_target=Rose") != std::string::npos
                && replayMetadata.find("compiler failed") == std::string::npos,
            "repair validation metadata should expose only Rose-owned request coordinates, not raw diagnostic output");

        rose::agent::observeValidationResult(
            replayState,
            failedBuildRequest,
            rose::tools::ToolResult{
                .success = true,
                .message = "build passed",
                .responseMode = rose::tools::ToolResponseMode::RequiresModelSynthesis,
                .artifacts = {}
            });
        require(
            !replayState.failedValidationRequest.has_value()
                && !replayState.ready,
            "successful replayed validation should clear the repair replay chain");

        rose::agent::RepairValidationReplayState mismatchedReplay;
        rose::agent::observeValidationResult(
            mismatchedReplay,
            failedBuildRequest,
            failedBuildResult);
        rose::agent::SourceRepairPlan mismatchedPlan = *repairPlan;
        mismatchedPlan.diagnostic->producerTool = "run_cmake_tests";
        rose::agent::observeCompletedRepair(
            mismatchedReplay,
            mismatchedPlan,
            successfulEditResult);
        require(
            !mismatchedReplay.ready,
            "repair must not inherit a validation request from a different diagnostic producer");

        // Batch 47: track the semantic outcome of a repair separately from the
        // mechanical fact that its patch was written. Only the exact correlated
        // validation may promote an applied patch to a proven repair.
        rose::agent::RepairOutcomeState repairOutcome;
        const auto appliedTransition =
            rose::agent::observeAppliedRepairOutcome(
                repairOutcome,
                *repairPlan,
                6,
                successfulEditResult,
                failedBuildRequest);

        require(
            appliedTransition == rose::agent::RepairOutcomeTransition::PatchApplied
                && repairOutcome.status
                    == rose::agent::RepairOutcomeStatus::PatchAppliedPendingValidation,
            "successful diagnostic-bound edit should be recorded as applied but still awaiting validation");

        const std::string pendingOutcomeMetadata =
            rose::agent::formatRepairOutcomeMetadata(repairOutcome);
        require(
            pendingOutcomeMetadata.find("status=patch_applied_pending_validation")
                    != std::string::npos
                && pendingOutcomeMetadata.find("repair_step=6")
                    != std::string::npos
                && pendingOutcomeMetadata.find("validation_tool=build_cmake_project")
                    != std::string::npos
                && pendingOutcomeMetadata.find("NOT yet proven successful")
                    != std::string::npos
                && pendingOutcomeMetadata.find("new one") == std::string::npos
                && pendingOutcomeMetadata.find("compiler failed") == std::string::npos,
            "repair outcome metadata should distinguish patch application from proof without retaining raw source/compiler text");

        const rose::tools::ToolRequest unrelatedValidation{
            .toolId = "run_cmake_tests",
            .arguments = {
                { "source_path", "C:/Rose" },
                { "configuration", "Debug" },
                { "jobs", "8" }
            }
        };

        rose::agent::RepairOutcomeState mismatchedOutcome;
        (void)rose::agent::observeAppliedRepairOutcome(
            mismatchedOutcome,
            *repairPlan,
            7,
            successfulEditResult,
            unrelatedValidation);
        require(
            mismatchedOutcome.status
                    == rose::agent::RepairOutcomeStatus::PatchAppliedUnverified
                && !mismatchedOutcome.validationRequest.has_value(),
            "repair outcome tracking must fail closed when the retained validation tool does not match the diagnostic producer");

        const auto unrelatedTransition =
            rose::agent::observeRepairValidationOutcome(
                repairOutcome,
                unrelatedValidation,
                rose::tools::ToolResult{
                    .success = true,
                    .message = "different validation passed",
                    .responseMode = rose::tools::ToolResponseMode::RequiresModelSynthesis,
                    .artifacts = {}
                });
        require(
            unrelatedTransition == rose::agent::RepairOutcomeTransition::None
                && repairOutcome.status
                    == rose::agent::RepairOutcomeStatus::PatchAppliedPendingValidation,
            "an unrelated validation command must not prove a tracked repair");

        const auto validatedTransition =
            rose::agent::observeRepairValidationOutcome(
                repairOutcome,
                failedBuildRequest,
                rose::tools::ToolResult{
                    .success = true,
                    .message = "build passed",
                    .responseMode = rose::tools::ToolResponseMode::RequiresModelSynthesis,
                    .artifacts = {}
                });
        require(
            validatedTransition == rose::agent::RepairOutcomeTransition::ValidationSucceeded
                && repairOutcome.status
                    == rose::agent::RepairOutcomeStatus::ValidationSucceeded
                && rose::agent::formatRepairOutcomeMetadata(repairOutcome).find(
                       "proven successful by that validation")
                    != std::string::npos,
            "only the exact correlated successful validation should prove the repair");

        rose::agent::RepairOutcomeState failedOutcome;
        (void)rose::agent::observeAppliedRepairOutcome(
            failedOutcome,
            *repairPlan,
            9,
            successfulEditResult,
            failedBuildRequest);
        const auto failedTransition =
            rose::agent::observeRepairValidationOutcome(
                failedOutcome,
                failedBuildRequest,
                failedBuildResult);
        require(
            failedTransition == rose::agent::RepairOutcomeTransition::ValidationFailed
                && failedOutcome.status
                    == rose::agent::RepairOutcomeStatus::ValidationFailed
                && failedOutcome.validationProducedGroundedDiagnostic,
            "failed exact validation should leave the patch applied but explicitly unproven and retain only whether fresh grounded diagnostics exist");

        const std::string failedJournalDetail =
            rose::agent::formatRepairOutcomeJournalDetail(failedOutcome);
        require(
            failedJournalDetail.find("repair_step=9") != std::string::npos
                && failedJournalDetail.find("status=validation_failed")
                    != std::string::npos
                && failedJournalDetail.find("diagnostic_code=C2065")
                    != std::string::npos
                && failedJournalDetail.find("new one") == std::string::npos,
            "repair outcome journal correlation should remain compact and source-text free");

        std::filesystem::remove_all(root, error);
        std::cout << "Rose AgentExecution tests: PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rose AgentExecution tests: FAIL: "
                  << exception.what() << '\n';
        return 1;
    }
}
