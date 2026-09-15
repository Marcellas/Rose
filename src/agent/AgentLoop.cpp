#include "agent/AgentLoop.h"

#include "agent/AgentJournal.h"
#include "agent/AgentTypes.h"
#include "agent/ToolObservation.h"
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
        permissions::ToolExecutionPolicy& executionPolicy,
        AgentJournal& journal,
        logging::Logger& logger,
        AgentLoopConfig config)
        : selectionAgent_{ selectionAgent }
        , toolRegistry_{ toolRegistry }
        , executionPolicy_{ executionPolicy }
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
        std::string initialTransientContext)
    {
        if (userText.empty())
        {
            throw std::invalid_argument{
                "AgentLoop cannot start with an empty user request."
            };
        }

        const std::uint64_t runId =
            journal_.beginRun(
                userText);

        AgentRunState state{
            .runId = runId,
            .originalUserText = std::move(userText),
            .transientContext = std::move(initialTransientContext),
            .executedToolCount = 0,
            .executedRequestFingerprints = {}
        };

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

        auto finishReady =
            [&](const bool reachedLimit = false)
            {
                journal_.record(
                    AgentEvent{
                        .runId = state.runId,
                        .type = AgentEventType::RunCompleted,
                        .stepIndex = 0,
                        .toolId = {},
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

                return std::move(result);
            };

        auto executeOne =
            [&](const tools::ToolRequest& request,
                const permissions::ToolConfirmationState confirmation)
                -> bool
            {
                const std::size_t stepIndex =
                    state.executedToolCount + 1;

                const tools::ITool* tool =
                    toolRegistry_.find(
                        request.toolId);

                if (tool == nullptr)
                {
                    throw std::runtime_error{
                        "Agent proposed a tool that is no longer registered: "
                        + request.toolId
                    };
                }

                const permissions::ToolExecutionDecision policyDecision =
                    executionPolicy_.evaluate(
                        tool->descriptor(),
                        confirmation);

                if (
                    policyDecision.disposition
                    == permissions::ToolExecutionDisposition::RequiresConfirmation)
                {
                    journal_.recordToolRequest(
                        state.runId,
                        AgentEventType::ConfirmationRequired,
                        stepIndex,
                        request,
                        policyDecision.reason);

                    PendingAgentRun pending{
                        .state = std::move(state),
                        .confirmation =
                            makePendingToolConfirmation(
                                request,
                                tool->descriptor())
                    };

                    result.status =
                        AgentLoopStatus::RequiresConfirmation;

                    result.userTextForResponse =
                        pending.state.originalUserText;

                    result.totalExecutedTools =
                        pending.state.executedToolCount;

                    result.pending =
                        std::move(pending);

                    return false;
                }

                if (!policyDecision.allowed())
                {
                    journal_.recordToolRequest(
                        state.runId,
                        AgentEventType::PolicyDenied,
                        stepIndex,
                        request,
                        policyDecision.reason);

                    throw std::runtime_error{
                        policyDecision.reason
                    };
                }

                const std::string fingerprint =
                    requestFingerprint(
                        request);

                if (alreadyExecuted(state, fingerprint))
                {
                    logger_.debug(
                        "AgentLoop",
                        "Blocked repeated identical tool request: "
                        + request.toolId);

                    journal_.recordToolRequest(
                        state.runId,
                        AgentEventType::DuplicateActionBlocked,
                        stepIndex,
                        request,
                        "Blocked an identical tool request that already executed in this run.");

                    appendTransientContext(
                        state.transientContext,
                        repeatedRequestGuard(request));

                    // false here means stop driving, but this is NOT a
                    // confirmation pause. The caller distinguishes using result.
                    result.status =
                        AgentLoopStatus::ReadyForResponse;

                    return false;
                }

                journal_.recordToolRequest(
                    state.runId,
                    AgentEventType::ToolStarted,
                    stepIndex,
                    request,
                    "Tool execution started.");

                const auto startedAt =
                    std::chrono::steady_clock::now();

                tools::ToolResult toolResult;

                try
                {
                    toolResult =
                        toolRegistry_.execute(
                            request);
                }
                catch (const std::exception& exception)
                {
                    const auto duration =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now()
                            - startedAt);

                    journal_.recordToolRequest(
                        state.runId,
                        AgentEventType::ToolFailed,
                        stepIndex,
                        request,
                        exception.what(),
                        duration);

                    throw;
                }
                catch (...)
                {
                    const auto duration =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now()
                            - startedAt);

                    journal_.recordToolRequest(
                        state.runId,
                        AgentEventType::ToolFailed,
                        stepIndex,
                        request,
                        "Tool execution failed with an unknown exception.",
                        duration);

                    throw;
                }

                const auto duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now()
                        - startedAt);

                journal_.recordToolRequest(
                    state.runId,
                    AgentEventType::ToolFinished,
                    stepIndex,
                    request,
                    toolResult.message.empty()
                        ? "Tool execution completed."
                        : toolResult.message,
                    duration);

                appendTransientContext(
                    state.transientContext,
                    buildToolObservation(
                        request,
                        toolResult));

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
                    + request.toolId);

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

            const AgentDecision decision =
                selectionAgent_.decide(
                    state.originalUserText,
                    state.transientContext);

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
