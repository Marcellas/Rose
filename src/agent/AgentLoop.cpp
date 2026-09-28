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
#include "agent/UserTaskContinuation.h"
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

            const std::string codingWorkspace =
                formatCodingTaskWorkspaceMetadata(
                    state.codingTaskWorkspace);

            if (!codingWorkspace.empty())
            {
                if (!metadata.empty())
                {
                    metadata += "\n";
                }
                metadata += codingWorkspace;
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
        bool completedRecursiveScanCoversListing(
            const AgentRunState& state,
            const tools::ToolRequest& request)
        {
            if (request.toolId != "list_directory")
            {
                return false;
            }

            const auto path =
                request.arguments.find("path");

            if (path == request.arguments.end())
            {
                return false;
            }

            const std::string pathFingerprint =
                "4:path="
                + std::to_string(path->second.size())
                + ":"
                + path->second
                + "\n";

            for (const std::string& fingerprint :
                 state.executedRequestFingerprints)
            {
                if (
                    fingerprint.starts_with("scan_directory_tree\n")
                    && fingerprint.find(pathFingerprint)
                        != std::string::npos)
                {
                    return true;
                }
            }

            return false;
        }


        [[nodiscard]]
        std::optional<std::string> fingerprintArgumentValue(
            const std::string& fingerprint,
            const std::string_view name)
        {
            const std::string marker =
                std::to_string(name.size())
                + ":"
                + std::string{ name }
                + "=";

            const std::size_t markerPosition =
                fingerprint.find(marker);

            if (markerPosition == std::string::npos)
            {
                return std::nullopt;
            }

            const std::size_t lengthBegin =
                markerPosition + marker.size();
            const std::size_t lengthEnd =
                fingerprint.find(':', lengthBegin);

            if (lengthEnd == std::string::npos)
            {
                return std::nullopt;
            }

            std::size_t valueLength{ 0 };
            try
            {
                valueLength = static_cast<std::size_t>(
                    std::stoull(
                        fingerprint.substr(
                            lengthBegin,
                            lengthEnd - lengthBegin)));
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }

            const std::size_t valueBegin = lengthEnd + 1u;
            if (valueBegin > fingerprint.size()
                || valueLength > fingerprint.size() - valueBegin)
            {
                return std::nullopt;
            }

            return fingerprint.substr(
                valueBegin,
                valueLength);
        }


        [[nodiscard]]
        std::optional<std::size_t> positiveSizeArgument(
            const std::string_view value)
        {
            if (value.empty())
            {
                return std::nullopt;
            }

            std::size_t consumed{ 0 };
            unsigned long long parsed{ 0 };

            try
            {
                parsed = std::stoull(
                    std::string{ value },
                    &consumed,
                    10);
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }

            if (consumed != value.size() || parsed == 0)
            {
                return std::nullopt;
            }

            return static_cast<std::size_t>(parsed);
        }


        [[nodiscard]]
        bool completedTextReadOverlaps(
            const AgentRunState& state,
            const tools::ToolRequest& request)
        {
            if (request.toolId != "read_text_file")
            {
                return false;
            }

            const auto path = request.arguments.find("path");
            if (path == request.arguments.end())
            {
                return false;
            }

            const auto currentStartIt = request.arguments.find("start_line");
            const auto currentCountIt = request.arguments.find("line_count");
            const bool currentWholeFile =
                currentStartIt == request.arguments.end()
                && currentCountIt == request.arguments.end();

            std::optional<std::size_t> currentStart;
            std::optional<std::size_t> currentCount;

            if (!currentWholeFile)
            {
                if (currentStartIt == request.arguments.end()
                    || currentCountIt == request.arguments.end())
                {
                    return false;
                }

                currentStart = positiveSizeArgument(currentStartIt->second);
                currentCount = positiveSizeArgument(currentCountIt->second);
                if (!currentStart.has_value() || !currentCount.has_value())
                {
                    return false;
                }
            }

            for (const std::string& fingerprint : state.executedRequestFingerprints)
            {
                if (!fingerprint.starts_with("read_text_file\n"))
                {
                    continue;
                }

                const auto previousPath =
                    fingerprintArgumentValue(fingerprint, "path");
                if (!previousPath.has_value() || *previousPath != path->second)
                {
                    continue;
                }

                const auto previousStartText =
                    fingerprintArgumentValue(fingerprint, "start_line");
                const auto previousCountText =
                    fingerprintArgumentValue(fingerprint, "line_count");

                const bool previousWholeFile =
                    !previousStartText.has_value()
                    && !previousCountText.has_value();

                if (currentWholeFile || previousWholeFile)
                {
                    return true;
                }

                if (!previousStartText.has_value()
                    || !previousCountText.has_value())
                {
                    continue;
                }

                const auto previousStart =
                    positiveSizeArgument(*previousStartText);
                const auto previousCount =
                    positiveSizeArgument(*previousCountText);

                if (!previousStart.has_value() || !previousCount.has_value())
                {
                    continue;
                }

                // read_text_file source windows are bounded to small line counts,
                // so these one-based half-open endpoints cannot approach size_t
                // overflow in a valid request.
                const std::size_t currentEnd =
                    *currentStart + *currentCount;
                const std::size_t previousEnd =
                    *previousStart + *previousCount;

                if (*currentStart < previousEnd
                    && *previousStart < currentEnd)
                {
                    return true;
                }
            }

            return false;
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
        std::string repeatedReadOnlyRequestRecheckGuard(
            const tools::ToolRequest& request)
        {
            std::ostringstream text;

            text
                << "<rose_agent_guard>\n"
                << "reason=repeated_read_only_tool_request\n"
                << "tool_id="
                << request.toolId
                << "\n"
                << "Rose already completed this exact read-only action in the current "
                   "agent run. No second execution occurred.\n"
                << "</rose_agent_guard>\n"
                << "Re-evaluate the ORIGINAL request once. If more evidence is still "
                   "needed, choose a DIFFERENT grounded read-only action or a "
                   "non-overlapping source window. Do not broaden the same read by "
                   "dropping start_line/line_count. If the available evidence is "
                   "already sufficient, choose RESPOND.";

            return text.str();
        }


        [[nodiscard]]
        std::string overlappingTextReadRecheckGuard(
            const tools::ToolRequest& request)
        {
            std::ostringstream text;

            text
                << "<rose_agent_guard>\n"
                << "reason=overlapping_text_read\n"
                << "tool_id=read_text_file\n";

            const auto path = request.arguments.find("path");
            if (path != request.arguments.end())
            {
                text
                    << "path="
                    << path->second
                    << "\n";
            }

            text
                << "A source window for this path already completed and the newly "
                   "requested read overlaps evidence Rose still has in this run. "
                   "No second read occurred.\n"
                << "</rose_agent_guard>\n"
                << "Re-evaluate the ORIGINAL request once. If more source evidence "
                   "is needed, choose a NON-OVERLAPPING start_line/line_count window "
                   "or a different grounded file. Do not widen an earlier window by "
                   "restarting from the same first line. If current evidence is enough, "
                   "choose RESPOND.";

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


        [[nodiscard]]
        std::string finalResponseTransientContext(
            const AgentRunState& state)
        {
            std::string context = state.transientContext;

            // The full capability contract is useful to the hidden routing model,
            // especially before any tool has run. After Rose has real execution
            // evidence it becomes redundant baggage for the user-facing response
            // model. Removing only this Rose-authored tagged block preserves tool
            // observations/project evidence while reclaiming several thousand
            // prompt tokens for large read-only analysis results.
            if (state.executedToolCount == 0)
            {
                return context;
            }

            constexpr std::string_view beginTag{
                "<rose_capability_contract>"
            };
            constexpr std::string_view endTag{
                "</rose_capability_contract>"
            };

            const std::size_t begin = context.find(beginTag);
            if (begin == std::string::npos)
            {
                return context;
            }

            const std::size_t rawEnd =
                context.find(endTag, begin + beginTag.size());
            if (rawEnd == std::string::npos)
            {
                return context;
            }

            std::size_t eraseEnd = rawEnd + endTag.size();
            while (
                eraseEnd < context.size()
                && (context[eraseEnd] == '\r'
                    || context[eraseEnd] == '\n'))
            {
                ++eraseEnd;
            }

            context.erase(begin, eraseEnd - begin);
            return context;
        }


        [[nodiscard]]
        bool toolCompletesWithFinalSynthesis(
            const std::string_view toolId) noexcept
        {
            // analyze_directory_documents already performs the bounded recursive
            // read requested by the user. Its excerpts are for RoseCore to
            // synthesize into the answer; sending the same large evidence through
            // another all-tools routing pass wastes context and can overflow the
            // local model before the final response is produced.
            return toolId == "analyze_directory_documents";
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
        AgentRunProvenance provenance,
        std::string priorUserTaskContext)
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
            .priorUserTaskContext = std::move(priorUserTaskContext),
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

        // A small local model may accidentally request the exact same read-only
        // action twice while investigating a broad diagnosis. Allow one control
        // re-check without consuming the execution budget so Rose can pick a
        // different grounded observation instead of ending the investigation
        // immediately. A second repeat still stops normally.
        bool readOnlyDuplicateRecheckUsed{
            false
        };

        // A widening/overlapping source read can consume context without adding
        // proportionate evidence. Give the control model one chance to move to a
        // non-overlapping window or a different file; a second overlap ends the
        // bounded investigation instead of looping.
        bool overlappingTextReadRecheckUsed{
            false
        };

        // A multi-file source mutation may be blocked once so the control model
        // can produce an explicit review plan. If it ignores the same guard again,
        // stop rather than spinning without consuming the tool-execution budget.
        bool codingPlanRequirementRaised{
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
                    finalResponseTransientContext(state);

                result.totalExecutedTools =
                    state.executedToolCount;

                result.reachedToolLimit =
                    reachedLimit;

                result.authoritativeResponse =
                    state.toolCompletion.
                        authoritativeResponseIfComplete(
                            state.executedToolCount);

                // Preserve a short user-only continuation only when NO tool has
                // executed yet and the combined user request still looks tool-backed.
                // This allows a later clarification such as "Scientific..." or
                // "use airspeed_calculator.cpp" to continue the same request without
                // treating assistant prose as execution authority.
                if (state.executedToolCount == 0)
                {
                    const std::string combinedUserTask =
                        appendUserTaskContinuationText(
                            state.priorUserTaskContext,
                            state.originalUserText);

                    if (CapabilityRoutingGuard::likelyToolBackedRequest(
                            combinedUserTask,
                            toolRegistry_))
                    {
                        result.preserveUserTaskContinuation = true;
                        result.userTaskContinuationContext =
                            std::move(combinedUserTask);
                    }
                }

                return std::move(result);
            };

        auto executeOne =
            [&](const tools::ToolRequest& request,
                const permissions::ToolConfirmationState confirmation)
                -> bool
            {
                const std::size_t stepIndex =
                    state.executedToolCount + 1;

                // Once Rose has observed multiple source files in this bounded
                // coding run, the first source write must have a human-reviewable
                // task plan. The plan itself grants no authority; it only makes the
                // larger intended sequence visible before normal write confirmation.
                if (
                    confirmation
                        != permissions::ToolConfirmationState::ExplicitlyConfirmed
                    && !state.codingTaskPlan.has_value()
                    && codingTaskPlanRequiredBeforeRequest(
                        state.codingTaskWorkspace,
                        request))
                {
                    if (codingPlanRequirementRaised)
                    {
                        appendTransientContext(
                            state.transientContext,
                            CapabilityRoutingGuard::buildExecutionEvidenceGuard(
                                state.executedToolCount));

                        result.status =
                            AgentLoopStatus::ReadyForResponse;
                        return false;
                    }

                    codingPlanRequirementRaised = true;

                    journal_.record(
                        AgentEvent{
                            .runId = state.runId,
                            .type = AgentEventType::CodingPlanRequired,
                            .stepIndex = stepIndex,
                            .toolId = request.toolId,
                            .projectId = {},
                            .discussionId = {},
                            .message =
                                "Blocked the first multi-file source mutation until the control model creates a bounded review plan.",
                            .detail = {}
                        });

                    appendTransientContext(
                        state.transientContext,
                        buildCodingTaskPlanRequiredContext());

                    logger_.debug(
                        "AgentLoop",
                        "Blocked a multi-file source mutation until a bounded coding plan is created.");

                    return true;
                }

                // A multi-file coding run may have read another source file
                // after the one now being edited. Recover the best still-valid
                // Rose-owned window for THIS requested path/range rather than
                // relying only on the most recent read.
                const std::optional<tools::SourceWindowEvidence>
                    retainedSourceWindow =
                        sourceWindowForRequest(
                            state.codingTaskWorkspace,
                            request);

                const std::optional<tools::SourceWindowEvidence>
                    repairSourceWindow =
                        retainedSourceWindow.has_value()
                            ? retainedSourceWindow
                            : state.latestSourceWindowEvidence;

                const tools::ToolRequest effectiveRequest =
                    bindSourceWindowEvidence(
                        request,
                        repairSourceWindow);

                const std::optional<SourceRepairPlan> repairPlan =
                    buildSourceRepairPlan(
                        effectiveRequest,
                        repairSourceWindow,
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
                    const tools::ITool* repeatedTool =
                        toolRegistry_.find(
                            effectiveRequest.toolId);

                    const bool repeatedReadOnly =
                        repeatedTool != nullptr
                        && repeatedTool->descriptor().risk
                            == tools::ToolRisk::ReadOnly;

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

                    if (
                        repeatedReadOnly
                        && !readOnlyDuplicateRecheckUsed)
                    {
                        readOnlyDuplicateRecheckUsed =
                            true;

                        appendTransientContext(
                            state.transientContext,
                            repeatedReadOnlyRequestRecheckGuard(
                                effectiveRequest));

                        logger_.debug(
                            "AgentLoop",
                            "Scheduled one bounded control re-check after a duplicate read-only action.");

                        return true;
                    }

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

                if (
                    effectiveRequest.toolId == "read_text_file"
                    && completedTextReadOverlaps(state, effectiveRequest))
                {
                    logger_.debug(
                        "AgentLoop",
                        "Blocked overlapping read_text_file source window that would repeat already-observed lines.");

                    journal_.recordToolRequest(
                        state.runId,
                        AgentEventType::DuplicateActionBlocked,
                        stepIndex,
                        effectiveRequest,
                        "Blocked an overlapping read_text_file request that would repeat already-observed source lines.");

                    if (!overlappingTextReadRecheckUsed)
                    {
                        overlappingTextReadRecheckUsed = true;

                        appendTransientContext(
                            state.transientContext,
                            overlappingTextReadRecheckGuard(
                                effectiveRequest));

                        logger_.debug(
                            "AgentLoop",
                            "Scheduled one bounded control re-check after an overlapping source-window read.");

                        return true;
                    }

                    appendTransientContext(
                        state.transientContext,
                        repeatedRequestGuard(effectiveRequest));

                    result.status =
                        AgentLoopStatus::ReadyForResponse;

                    return false;
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

                    PendingToolConfirmation confirmationSummary =
                        makePendingToolConfirmation(
                            effectiveRequest,
                            execution.descriptor,
                            repairPlan);

                    if (state.codingTaskPlan.has_value())
                    {
                        const std::string planSummary =
                            formatCodingTaskPlanForConfirmation(
                                *state.codingTaskPlan,
                                effectiveRequest);

                        if (!planSummary.empty())
                        {
                            confirmationSummary.userFacingSummary =
                                planSummary
                                + "\n"
                                + confirmationSummary.userFacingSummary;
                        }
                    }

                    PendingAgentRun pending{
                        .state = std::move(state),
                        .confirmation = std::move(confirmationSummary)
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

                // Retain validated source windows across reads of other files.
                // The workspace stores only path/range/hash provenance, never raw
                // source text. Successful mutations invalidate the affected path.
                if (toolResult.sourceWindowEvidence.has_value())
                {
                    observeCodingSourceWindow(
                        state.codingTaskWorkspace,
                        *toolResult.sourceWindowEvidence);
                }

                observeCodingMutation(
                    state.codingTaskWorkspace,
                    effectiveRequest,
                    toolResult);

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

                if (toolCompletesWithFinalSynthesis(effectiveRequest.toolId))
                {
                    logger_.debug(
                        "AgentLoop",
                        "Completed terminal read-only analysis; handing bounded "
                        "evidence directly to final response synthesis.");
                    return false;
                }

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
                    trustedControlMetadata,
                    state.priorUserTaskContext);

            const bool invokesTool =
                decision.action == AgentAction::InvokeTool
                && decision.toolRequest.has_value();

            const bool createsCodingPlan =
                decision.action == AgentAction::PlanCodingTask
                && decision.codingTaskPlan.has_value();

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
                            : (createsCodingPlan
                                ? "Control model prepared a bounded multi-file coding plan."
                                : "Control model selected normal response as the next action."),
                    .detail =
                        "control_output_bytes="
                        + std::to_string(
                            decision.rawModelOutput.size())
                });

            if (createsCodingPlan)
            {
                if (state.codingTaskPlan.has_value())
                {
                    appendTransientContext(
                        state.transientContext,
                        "<rose_coding_plan_guard>\nreason=plan_already_exists\n"
                        "A bounded coding plan already exists for this run. Do not plan "
                        "again; continue with the next grounded tool action or RESPOND.\n"
                        "</rose_coding_plan_guard>");

                    return finishReady();
                }

                state.codingTaskPlan =
                    *decision.codingTaskPlan;

                const std::string planContext =
                    formatCodingTaskPlanContext(
                        *state.codingTaskPlan);

                appendTransientContext(
                    state.transientContext,
                    planContext);

                journal_.record(
                    AgentEvent{
                        .runId = state.runId,
                        .type = AgentEventType::CodingPlanCreated,
                        .stepIndex = nextStepIndex,
                        .toolId = {},
                        .projectId = {},
                        .discussionId = {},
                        .message =
                            "Stored a bounded advisory coding plan before multi-file mutation.",
                        .detail = planContext
                    });

                logger_.debug(
                    "AgentLoop",
                    "Stored a bounded multi-file coding plan for confirmation review.");

                continue;
            }

            if (!invokesTool)
            {
                const std::string routingUserText =
                    appendUserTaskContinuationText(
                        state.priorUserTaskContext,
                        state.originalUserText);

                const bool originalLooksToolBacked =
                    CapabilityRoutingGuard::likelyToolBackedRequest(
                        routingUserText,
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
                        routingUserText,
                        toolRegistry_,
                        completed,
                        state.transientContext);

                const bool likelyToolRequest =
                    originalLooksToolBacked
                    || directRequest.has_value()
                    || diagnosticRead.has_value();

                const bool directRequestIsRedundantListing =
                    directRequest.has_value()
                    && completedRecursiveScanCoversListing(
                        state,
                        *directRequest);

                // A broad project-diagnosis fallback may reconstruct a root
                // list_directory request even after scan_directory_tree already
                // covered that root. If a failed configure/build/test also produced
                // one grounded diagnostic source window, the diagnostic is the
                // useful continuation and must not be masked by the redundant list.
                const std::optional<tools::ToolRequest> recoverableRequest =
                    diagnosticRead.has_value()
                            && directRequestIsRedundantListing
                        ? diagnosticRead
                        : (directRequest.has_value()
                               ? directRequest
                               : diagnosticRead);


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

                        // A completed recursive scan already includes the root's
                        // immediate entries. Deterministic recovery must not walk
                        // backward into list_directory merely because the small
                        // control model chose RESPOND after broader discovery.
                        if (completedRecursiveScanCoversListing(
                                state,
                                recovered))
                        {
                            logger_.debug(
                                "AgentLoop",
                                "Skipped redundant deterministic list_directory because the same root was already recursively scanned.");
                            return finishReady();
                        }

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
