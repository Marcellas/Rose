#include "agent/AgentLoop.h"

#include "agent/CapabilityRoutingGuard.h"
#include "agent/AgentJournal.h"
#include "agent/AgentTypes.h"
#include "agent/ToolObservation.h"
#include "agent/SourceWindowBinding.h"
#include "agent/SourceRepairPlan.h"
#include "agent/RepairValidationReplay.h"
#include "agent/RepairOutcomeTracker.h"
#include "agent/ToolExecutionService.h"
#include "agent/ToolSelectionAgent.h"
#include "logging/Logger.h"
#include "permissions/ToolExecutionPolicy.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTypes.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rose::agent
{
    namespace
    {
        void appendTransientContext(
            std::string& destination,
            const std::string_view block)
        {
            if (block.empty())
            {
                return;
            }

            if (!destination.empty())
            {
                destination += "\n\n";
            }

            destination.append(
                block.data(),
                block.size());
        }


        [[nodiscard]]
        std::string trustedMetadataForSelection(
            const AgentRunState& state)
        {
            // A diagnostic is intentionally allowed to survive one matching
            // source-window read. Present both Rose-owned metadata blocks to the
            // control model, but avoid duplicating the common just-failed-build
            // case where both fields contain the same diagnostic block.
            std::string metadata;

            if (state.latestDiagnosticMetadata.empty())
            {
                metadata = state.latestTrustedToolMetadata;
            }
            else if (
                state.latestTrustedToolMetadata.empty()
                || state.latestTrustedToolMetadata == state.latestDiagnosticMetadata)
            {
                metadata = state.latestDiagnosticMetadata;
            }
            else
            {
                metadata =
                    state.latestDiagnosticMetadata
                    + "\n"
                    + state.latestTrustedToolMetadata;
            }

            const std::string replay =
                formatRepairValidationReplayMetadata(
                    state.repairValidationReplay);

            if (!replay.empty())
            {
                if (!metadata.empty())
                {
                    metadata += "\n";
                }
                metadata += replay;
            }

            const std::string repairOutcome =
                formatRepairOutcomeMetadata(
                    state.repairOutcome);

            if (!repairOutcome.empty())
            {
                if (!metadata.empty())
                {
                    metadata += "\n";
                }
                metadata += repairOutcome;
            }

            return metadata;
        }



        [[nodiscard]]
        std::string requestFingerprint(
            const tools::ToolRequest& request)
        {
            std::vector<std::pair<std::string, std::string>> arguments;
            arguments.reserve(
                request.arguments.size());

            for (const auto& [name, value] : request.arguments)
            {
                arguments.emplace_back(
                    name,
                    value);
            }

            std::sort(
                arguments.begin(),
                arguments.end(),
                [](const auto& left, const auto& right)
                {
                    return left.first < right.first;
                });

            std::ostringstream text;
            text
                << request.toolId
                << '\n';

            for (const auto& [name, value] : arguments)
            {
                text
                    << name.size()
                    << ':'
                    << name
                    << '='
                    << value.size()
                    << ':'
                    << value
                    << '\n';
            }

            return text.str();
        }


        [[nodiscard]]
        std::vector<std::string_view> completedToolIds(
            const AgentRunState& state)
        {
            std::vector<std::string_view> ids;
            ids.reserve(
                state.executedRequestFingerprints.size());


            for (const std::string& fingerprint :
                 state.executedRequestFingerprints)
            {
                const std::size_t newline =
                    fingerprint.find('\n');


                if (
                    newline != std::string::npos
                    && newline > 0)
                {
                    ids.emplace_back(
                        fingerprint.data(),
                        newline);
                }
            }


            return ids;
        }


        [[nodiscard]]
        bool alreadyExecuted(
            const AgentRunState& state,
            const std::string_view fingerprint)
        {
            return std::find(
                       state.executedRequestFingerprints.begin(),
                       state.executedRequestFingerprints.end(),
                       fingerprint)
                != state.executedRequestFingerprints.end();
        }


        [[nodiscard]]
        std::string_view toolIdFromFingerprint(
            const std::string& fingerprint) noexcept
        {
            const std::size_t newline = fingerprint.find('\n');
            return newline == std::string::npos
                ? std::string_view{ fingerprint }
                : std::string_view{ fingerprint.data(), newline };
        }


        [[nodiscard]]
        bool validationRetryAllowedAfterMutation(
            const AgentRunState& state,
            const std::string_view fingerprint,
            const tools::ToolRegistry& registry)
        {
            // Re-running an identical configure/build/test validation is useful
            // only when local project state changed after its previous execution. This preserves
            // the global duplicate-action guard while permitting bounded
            // edit -> build -> test -> repair -> build -> test workflows.
            // Every mutation/configure/build/test still crosses normal policy/consent.
            if (!fingerprint.starts_with("build_cmake_project\n")
                && !fingerprint.starts_with("reconfigure_cmake_project\n")
                && !fingerprint.starts_with("run_cmake_tests\n"))
            {
                return false;
            }

            std::size_t previousValidation =
                state.executedRequestFingerprints.size();
            for (std::size_t index = state.executedRequestFingerprints.size();
                 index > 0;
                 --index)
            {
                if (state.executedRequestFingerprints[index - 1] == fingerprint)
                {
                    previousValidation = index - 1;
                    break;
                }
            }

            if (previousValidation == state.executedRequestFingerprints.size())
            {
                return false;
            }

            for (std::size_t index = previousValidation + 1;
                 index < state.executedRequestFingerprints.size();
                 ++index)
            {
                const std::string_view toolId =
                    toolIdFromFingerprint(state.executedRequestFingerprints[index]);
                const tools::ITool* tool = registry.find(toolId);
                if (tool == nullptr)
                {
                    continue;
                }

                const tools::ToolRisk risk = tool->descriptor().risk;
                if (risk == tools::ToolRisk::LocalWrite
                    || risk == tools::ToolRisk::Destructive)
                {
                    return true;
                }
            }

            return false;
        }


        [[nodiscard]]
        std::string repeatedRequestGuard(
            const tools::ToolRequest& request)
        {
            std::ostringstream text;

            text
                << "<rose_agent_guard>\n"
                << "reason=repeated_identical_tool_request\n"
                << "tool_id="
                << request.toolId
                << "\n"
                << "Rose refused to execute the exact same tool request twice in one "
                   "agent run. No second execution occurred.\n"
                << "</rose_agent_guard>\n"
                << "Respond to the user using the completed observations already "
                   "available. Do not claim that the repeated action ran again.";

            return text.str();
        }


        [[nodiscard]]
        std::string toolLimitGuard(
            const std::size_t limit)
        {
            std::ostringstream text;

            text
                << "<rose_agent_guard>\n"
                << "reason=maximum_tool_executions_reached\n"
                << "maximum_tool_executions="
                << limit
                << "\n"
                << "Rose stopped the agent workflow at its configured safety ceiling. "
                   "No further tools were executed.\n"
                << "</rose_agent_guard>\n"
                << "Give the user the best final response possible from the results "
                   "already available. If more work is needed, say so rather than "
                   "pretending additional actions completed.";

            return text.str();
        }


        void appendArtifacts(
            std::vector<artifacts::Artifact>& destination,
            std::vector<artifacts::Artifact> source)
        {
            destination.reserve(
                destination.size()
                + source.size());

            for (auto& artifact : source)
            {
                destination.push_back(
                    std::move(artifact));
            }
        }
    }


    AgentLoop::AgentLoop(
        ToolSelectionAgent& selectionAgent,
        tools::ToolRegistry& toolRegistry,
        ToolExecutionService& executionService,
        AgentJournal& journal,
        logging::Logger& logger,
        AgentLoopConfig config)
        : selectionAgent_{ selectionAgent }
        , toolRegistry_{ toolRegistry }
        , executionService_{ executionService }
        , journal_{ journal }
        , logger_{ logger }
        , config_{ config }
    {
        if (config_.maximumToolExecutions == 0)
        {
            throw std::invalid_argument{
                "AgentLoop maximumToolExecutions must be greater than zero."
            };
        }
    }


    AgentLoopResult AgentLoop::start(
        std::string userText,
        std::string initialTransientContext,
        AgentRunProvenance provenance)
    {
        if (userText.empty())
        {
            throw std::invalid_argument{
                "AgentLoop cannot start with an empty user request."
            };
        }

        const std::uint64_t runId =
            journal_.beginRun(
                userText,
                std::move(provenance));

        AgentRunState state{
            .runId = runId,
            .originalUserText = std::move(userText),
            .transientContext = std::move(initialTransientContext),
            .executedToolCount = 0,
            .executedRequestFingerprints = {},
            .toolCompletion = {}
        };


        // ToolRegistry is the source of truth for Rose's current operational
        // capabilities. The contract remains transient and follows the same
        // AgentRunState through any confirmation pause.
        appendTransientContext(
            state.transientContext,
            CapabilityRoutingGuard::buildCapabilityContract(
                toolRegistry_));


        try
        {
            return drive(
                std::move(state),
                std::nullopt);
        }
        catch (const std::exception& exception)
        {
            journal_.record(
                AgentEvent{
                    .runId = runId,
                    .type = AgentEventType::RunFailed,
                    .stepIndex = 0,
                    .toolId = {},
                    .projectId = {},
                    .discussionId = {},
                    .message = exception.what(),
                    .detail = {}
                });

            throw;
        }
        catch (...)
        {
            journal_.record(
                AgentEvent{
                    .runId = runId,
                    .type = AgentEventType::RunFailed,
                    .stepIndex = 0,
                    .toolId = {},
                    .projectId = {},
                    .discussionId = {},
                    .message = "Agent run failed with an unknown exception.",
                    .detail = {}
                });

            throw;
        }
    }


    AgentLoopResult AgentLoop::resumeConfirmed(
        PendingAgentRun pending)
    {
        const std::uint64_t runId =
            pending.state.runId;

        const std::size_t confirmedStepIndex =
            pending.state.executedToolCount + 1;

        journal_.recordToolRequest(
            runId,
            AgentEventType::ConfirmationGranted,
            confirmedStepIndex,
            pending.confirmation.request,
            "User explicitly confirmed the exact pending request.");

        try
        {
            return drive(
                std::move(pending.state),
                std::move(pending.confirmation.request));
        }
        catch (const std::exception& exception)
        {
            journal_.record(
                AgentEvent{
                    .runId = runId,
                    .type = AgentEventType::RunFailed,
                    .stepIndex = 0,
                    .toolId = {},
                    .projectId = {},
                    .discussionId = {},
                    .message = exception.what(),
                    .detail = {}
                });

            throw;
        }
        catch (...)
        {
            journal_.record(
                AgentEvent{
                    .runId = runId,
                    .type = AgentEventType::RunFailed,
                    .stepIndex = 0,
                    .toolId = {},
                    .projectId = {},
                    .discussionId = {},
                    .message = "Agent run failed with an unknown exception.",
                    .detail = {}
                });

            throw;
        }
    }


    AgentLoopResult AgentLoop::drive(
        AgentRunState state,
        std::optional<tools::ToolRequest> explicitlyConfirmedRequest)
    {
        AgentLoopResult result;
        result.userTextForResponse =
            state.originalUserText;


        // A likely tool-backed request may receive one additional control-model
        // decision if the first pass incorrectly chooses normal response.
        bool capabilityRecheckUsed{
            false
        };


        auto finishReady =
            [&](const bool reachedLimit = false)
            {
                journal_.record(
                    AgentEvent{
                        .runId = state.runId,
                        .type = AgentEventType::RunCompleted,
                        .stepIndex = 0,
                        .toolId = {},
                        .projectId = {},
                        .discussionId = {},
                        .message =
                            reachedLimit
                                ? "Agent run completed at the configured tool execution ceiling."
                                : "Agent run is ready for final conversational response.",
                        .detail = {}
                    });

                result.status =
                    AgentLoopStatus::ReadyForResponse;

                result.userTextForResponse =
                    state.originalUserText;

                result.transientContext =
                    state.transientContext;

                result.totalExecutedTools =
                    state.executedToolCount;

                result.reachedToolLimit =
                    reachedLimit;

                result.authoritativeResponse =
                    state.toolCompletion.
                        authoritativeResponseIfComplete(
                            state.executedToolCount);

                return std::move(result);
            };

        auto executeOne =
            [&](const tools::ToolRequest& request,
                const permissions::ToolConfirmationState confirmation)
                -> bool
            {
                const std::size_t stepIndex =
                    state.executedToolCount + 1;

                const tools::ToolRequest effectiveRequest =
                    bindSourceWindowEvidence(
                        request,
                        state.latestSourceWindowEvidence);

                const std::optional<SourceRepairPlan> repairPlan =
                    buildSourceRepairPlan(
                        effectiveRequest,
                        state.latestSourceWindowEvidence,
                        state.latestDiagnosticMetadata);

                const std::string fingerprint =
                    requestFingerprint(
                        effectiveRequest);

                const bool duplicateRequest =
                    alreadyExecuted(state, fingerprint);
                const bool allowedValidationRetry =
                    duplicateRequest
                    && validationRetryAllowedAfterMutation(
                        state,
                        fingerprint,
                        toolRegistry_);

                if (duplicateRequest && !allowedValidationRetry)
                {
                    logger_.debug(
                        "AgentLoop",
                        "Blocked repeated identical tool request: "
                        + effectiveRequest.toolId);

                    journal_.recordToolRequest(
                        state.runId,
                        AgentEventType::DuplicateActionBlocked,
                        stepIndex,
                        effectiveRequest,
                        "Blocked an identical tool request that already executed in this run.");

                    appendTransientContext(
                        state.transientContext,
                        repeatedRequestGuard(effectiveRequest));

                    // false here means stop driving, but this is NOT a
                    // confirmation pause. The caller distinguishes using result.
                    result.status =
                        AgentLoopStatus::ReadyForResponse;

                    return false;
                }

                if (allowedValidationRetry)
                {
                    logger_.debug(
                        "AgentLoop",
                        "Allowed repeated developer validation after an intervening local mutation.");
                }

                ToolExecutionServiceResult execution =
                    executionService_.execute(
                        effectiveRequest,
                        state.runId,
                        stepIndex,
                        confirmation);

                if (execution.status
                    == ToolExecutionServiceStatus::RequiresConfirmation)
                {
                    if (repairPlan.has_value())
                    {
                        journal_.record(
                            AgentEvent{
                                .runId = state.runId,
                                .type = AgentEventType::RepairPlanned,
                                .stepIndex = stepIndex,
                                .toolId = effectiveRequest.toolId,
                                .projectId = {},
                                .discussionId = {},
                                .message =
                                    "Prepared a provenance-bound source repair plan before mutation.",
                                .detail = formatSourceRepairPlan(*repairPlan)
                            });
                    }

                    PendingAgentRun pending{
                        .state = std::move(state),
                        .confirmation =
                            makePendingToolConfirmation(
                                effectiveRequest,
                                execution.descriptor,
                                repairPlan)
                    };

                    result.status =
                        AgentLoopStatus::RequiresConfirmation;
                    result.userTextForResponse =
                        pending.state.originalUserText;
                    result.totalExecutedTools =
                        pending.state.executedToolCount;
                    result.pending = std::move(pending);
                    return false;
                }

                if (execution.status == ToolExecutionServiceStatus::Denied)
                {
                    throw std::runtime_error{ execution.reason };
                }

                tools::ToolResult toolResult =
                    std::move(execution.result);

                // If this is the exact validation retained for a previously
                // applied repair, classify its outcome BEFORE RepairValidationReplay
                // consumes/refreshes the failed-validation request. This cleanly
                // separates "patch applied" from "repair proven successful".
                const RepairOutcomeTransition validationOutcomeTransition =
                    observeRepairValidationOutcome(
                        state.repairOutcome,
                        effectiveRequest,
                        toolResult);

                observeValidationResult(
                    state.repairValidationReplay,
                    effectiveRequest,
                    toolResult);

                RepairOutcomeTransition repairOutcomeTransition{
                    RepairOutcomeTransition::None
                };

                if (repairPlan.has_value())
                {
                    observeCompletedRepair(
                        state.repairValidationReplay,
                        *repairPlan,
                        toolResult);

                    repairOutcomeTransition =
                        observeAppliedRepairOutcome(
                            state.repairOutcome,
                            *repairPlan,
                            stepIndex,
                            toolResult,
                            pendingRepairValidationRequest(
                                state.repairValidationReplay));
                }

                appendTransientContext(
                    state.transientContext,
                    buildToolObservation(
                        effectiveRequest,
                        toolResult));

                if (repairOutcomeTransition == RepairOutcomeTransition::PatchApplied)
                {
                    journal_.record(
                        AgentEvent{
                            .runId = state.runId,
                            .type = AgentEventType::RepairApplied,
                            .stepIndex = stepIndex,
                            .toolId = effectiveRequest.toolId,
                            .projectId = {},
                            .discussionId = {},
                            .message =
                                state.repairOutcome.status
                                        == RepairOutcomeStatus::PatchAppliedPendingValidation
                                    ? "Applied the provenance-bound source repair; correlated validation is still pending."
                                    : "Applied the provenance-bound source repair, but no exact correlated validation is available.",
                            .detail =
                                formatRepairOutcomeJournalDetail(
                                    state.repairOutcome)
                        });

                    appendTransientContext(
                        state.transientContext,
                        formatRepairOutcomeMetadata(
                            state.repairOutcome));
                }

                if (validationOutcomeTransition == RepairOutcomeTransition::ValidationSucceeded)
                {
                    journal_.record(
                        AgentEvent{
                            .runId = state.runId,
                            .type = AgentEventType::RepairValidated,
                            .stepIndex = stepIndex,
                            .toolId = effectiveRequest.toolId,
                            .projectId = {},
                            .discussionId = {},
                            .message =
                                "Exact correlated post-repair validation succeeded; the tracked repair is proven successful by that validation.",
                            .detail =
                                formatRepairOutcomeJournalDetail(
                                    state.repairOutcome)
                        });

                    appendTransientContext(
                        state.transientContext,
                        formatRepairOutcomeMetadata(
                            state.repairOutcome));
                }
                else if (validationOutcomeTransition == RepairOutcomeTransition::ValidationFailed)
                {
                    journal_.record(
                        AgentEvent{
                            .runId = state.runId,
                            .type = AgentEventType::RepairValidationFailed,
                            .stepIndex = stepIndex,
                            .toolId = effectiveRequest.toolId,
                            .projectId = {},
                            .discussionId = {},
                            .message =
                                "Exact correlated post-repair validation failed; the patch remains applied but the tracked repair is not proven successful.",
                            .detail =
                                formatRepairOutcomeJournalDetail(
                                    state.repairOutcome)
                        });

                    appendTransientContext(
                        state.transientContext,
                        formatRepairOutcomeMetadata(
                            state.repairOutcome));
                }

                // Keep structured routing metadata out of the ordinary transient
                // text channel. The generic latest block follows the newest tool,
                // while one grounded configure/compiler/test diagnostic may survive the
                // immediately following matching source-window read so a repair
                // confirmation can explain why that patch is being proposed.
                const bool newDiagnostic =
                    isSourceDiagnosticMetadata(toolResult.trustedMetadata);

                if (newDiagnostic)
                {
                    state.latestDiagnosticMetadata =
                        toolResult.trustedMetadata;
                }
                else if (effectiveRequest.toolId == "read_text_file")
                {
                    if (!sourceWindowCoversDiagnostic(
                            toolResult.sourceWindowEvidence,
                            state.latestDiagnosticMetadata))
                    {
                        state.latestDiagnosticMetadata.clear();
                    }
                }
                else
                {
                    // Any unrelated action or mutation makes the prior diagnostic
                    // stale for planning purposes. A later failed validation will
                    // contribute a fresh diagnostic block.
                    state.latestDiagnosticMetadata.clear();
                }

                state.latestTrustedToolMetadata =
                    toolResult.trustedMetadata;

                state.latestSourceWindowEvidence =
                    toolResult.sourceWindowEvidence;

                state.toolCompletion.observe(
                    toolResult);

                appendArtifacts(
                    result.artifacts,
                    std::move(toolResult.artifacts));

                state.executedRequestFingerprints.push_back(
                    fingerprint);

                ++state.executedToolCount;

                logger_.debug(
                    "AgentLoop",
                    "Executed tool step "
                    + std::to_string(state.executedToolCount)
                    + "/"
                    + std::to_string(config_.maximumToolExecutions)
                    + ": "
                    + effectiveRequest.toolId);

                return true;
            };


        // A confirmed request executes FIRST, before asking the model what the next
        // step should be. This preserves exact-action confirmation semantics.
        if (explicitlyConfirmedRequest.has_value())
        {
            const bool continued =
                executeOne(
                    *explicitlyConfirmedRequest,
                    permissions::ToolConfirmationState::ExplicitlyConfirmed);

            if (!continued)
            {
                if (
                    result.status
                    == AgentLoopStatus::RequiresConfirmation)
                {
                    // Defensive: an explicitly confirmed request should normally
                    // become Allowed. If future policy still asks for confirmation,
                    // return the pause rather than bypassing policy.
                    return result;
                }

                return finishReady();
            }
        }


        while (
            state.executedToolCount
            < config_.maximumToolExecutions)
        {
            const std::size_t nextStepIndex =
                state.executedToolCount + 1;

            // A successful provenance-bound repair has one deterministic next
            // action: re-run the exact failed configure/build/test request that produced
            // its diagnostic. Do this before another model pass so validation
            // arguments cannot drift. Policy still pauses for /confirm.
            if (const auto replay =
                    pendingRepairValidationRequest(
                        state.repairValidationReplay);
                replay.has_value())
            {
                journal_.recordToolRequest(
                    state.runId,
                    AgentEventType::ToolProposed,
                    nextStepIndex,
                    *replay,
                    "Deterministic post-repair validation replay proposed the exact failed configure/build/test request.");

                const bool continued =
                    executeOne(
                        *replay,
                        permissions::ToolConfirmationState::NotConfirmed);

                if (!continued)
                {
                    if (result.status == AgentLoopStatus::RequiresConfirmation)
                    {
                        return result;
                    }

                    return finishReady();
                }

                continue;
            }

            const std::string trustedControlMetadata =
                trustedMetadataForSelection(state);

            const AgentDecision decision =
                selectionAgent_.decide(
                    state.originalUserText,
                    state.transientContext,
                    trustedControlMetadata);

            const bool invokesTool =
                decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value();

            journal_.record(
                AgentEvent{
                    .runId = state.runId,
                    .type = AgentEventType::DecisionMade,
                    .stepIndex = invokesTool ? nextStepIndex : 0,
                    .toolId =
                        invokesTool
                            ? decision.toolRequest->toolId
                            : std::string{},
                    .projectId = {},
                    .discussionId = {},
                    .message =
                        invokesTool
                            ? "Control model selected a tool as the next action."
                            : "Control model selected normal response as the next action.",
                    .detail =
                        "control_output_bytes="
                        + std::to_string(
                            decision.rawModelOutput.size())
                });

            if (!invokesTool)
            {
                const bool originalLooksToolBacked =
                    CapabilityRoutingGuard::likelyToolBackedRequest(
                        state.originalUserText,
                        toolRegistry_);

                // A failed confirmed configure/build/test validation may contribute one Rose-owned,
                // project-grounded source diagnostic. During an explicit repair
                // workflow that diagnostic becomes a recoverable narrow source
                // read even when the ORIGINAL configure/build/test action is already done.
                const std::optional<tools::ToolRequest> diagnosticRead =
                    sourceWindowCoversDiagnostic(
                        state.latestSourceWindowEvidence,
                        state.latestDiagnosticMetadata)
                        ? std::nullopt
                        : CapabilityRoutingGuard::recoverDiagnosticSourceReadRequest(
                            state.originalUserText,
                            toolRegistry_,
                            state.latestDiagnosticMetadata.empty()
                                ? state.latestTrustedToolMetadata
                                : state.latestDiagnosticMetadata);

                // Deterministic recovery is the stronger signal: it has already
                // reconstructed one exact registered-tool request from the user's
                // words and Rose-owned context. Do not gate that reconstruction
                // behind the broader likelyToolBackedRequest heuristic. The latter
                // is intentionally approximate and may miss new phrasing as tools
                // evolve (for example, a line-oriented source mutation).
                const std::vector<std::string_view> completed =
                    completedToolIds(
                        state);

                const std::optional<tools::ToolRequest> directRequest =
                    CapabilityRoutingGuard::recoverDirectToolRequest(
                        state.originalUserText,
                        toolRegistry_,
                        completed,
                        state.transientContext);

                const bool likelyToolRequest =
                    originalLooksToolBacked
                    || directRequest.has_value()
                    || diagnosticRead.has_value();

                const std::optional<tools::ToolRequest> recoverableRequest =
                    directRequest.has_value()
                        ? directRequest
                        : diagnosticRead;


                // Re-check only while a concrete direct tool action remains
                // unsatisfied. Once generate_image has already completed for the
                // request, normal RESPOND should finish the run immediately.
                if (
                    !capabilityRecheckUsed
                    && recoverableRequest.has_value())
                {
                    capabilityRecheckUsed =
                        true;


                    appendTransientContext(
                        state.transientContext,
                        CapabilityRoutingGuard::buildRecheckGuard(
                            state.executedToolCount));


                    logger_.debug(
                        "AgentLoop",
                        "Control model selected RESPOND while a recoverable "
                        "registered-tool request remains; performing one bounded "
                        "capability re-check.");


                    journal_.record(
                        AgentEvent{
                            .runId = state.runId,
                            .type = AgentEventType::DecisionMade,
                            .stepIndex = 0,
                            .toolId = {},
                            .projectId = {},
                            .discussionId = {},
                            .message =
                                "Scheduled one bounded capability re-check after a "
                                "normal-response decision with a recoverable "
                                "tool request.",
                            .detail =
                                "capability_recheck=true"
                        });


                    continue;
                }


                if (likelyToolRequest)
                {
                    if (recoverableRequest.has_value())
                    {
                        const tools::ToolRequest& recovered =
                            *recoverableRequest;
                        logger_.debug(
                            "AgentLoop",
                            "Recovered a deterministic tool request after two "
                            "model routing passes selected RESPOND: "
                            + recovered.toolId);


                        journal_.recordToolRequest(
                            state.runId,
                            AgentEventType::ToolProposed,
                            nextStepIndex,
                            recovered,
                            "Deterministic recovery proposed this registered tool after two normal-response decisions.");


                        const bool continued =
                            executeOne(
                                recovered,
                                permissions::ToolConfirmationState::NotConfirmed);


                        if (!continued)
                        {
                            if (
                                result.status
                                == AgentLoopStatus::RequiresConfirmation)
                            {
                                return result;
                            }


                            appendTransientContext(
                                state.transientContext,
                                CapabilityRoutingGuard::buildExecutionEvidenceGuard(
                                    state.executedToolCount));


                            return finishReady();
                        }


                        continue;
                    }


                    // If we recognized the request as tool-like but cannot safely
                    // reconstruct an exact request (for example "read a file" with
                    // no absolute path), prevent the final model from hallucinating
                    // that the action already happened.
                    appendTransientContext(
                        state.transientContext,
                        CapabilityRoutingGuard::buildExecutionEvidenceGuard(
                            state.executedToolCount));
                }


                return finishReady();
            }

            journal_.recordToolRequest(
                state.runId,
                AgentEventType::ToolProposed,
                nextStepIndex,
                *decision.toolRequest,
                "Control model proposed this tool request.");

            const bool continued =
                executeOne(
                    *decision.toolRequest,
                    permissions::ToolConfirmationState::NotConfirmed);

            if (!continued)
            {
                if (
                    result.status
                    == AgentLoopStatus::RequiresConfirmation)
                {
                    return result;
                }

                return finishReady();
            }
        }


        journal_.record(
            AgentEvent{
                .runId = state.runId,
                .type = AgentEventType::StepLimitReached,
                .stepIndex = 0,
                .toolId = {},
                .projectId = {},
                .discussionId = {},
                .message =
                    "Stopped before another tool because the configured execution ceiling was reached.",
                .detail =
                    "maximum_tool_executions="
                    + std::to_string(config_.maximumToolExecutions)
            });

        appendTransientContext(
            state.transientContext,
            toolLimitGuard(
                config_.maximumToolExecutions));

        logger_.debug(
            "AgentLoop",
            "Stopped workflow at maximum tool execution count: "
            + std::to_string(config_.maximumToolExecutions));

        return finishReady(true);
    }

} // namespace rose::agent
