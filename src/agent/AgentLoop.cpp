#include "agent/AgentLoop.h"

#include "agent/AgentTypes.h"
#include "agent/ToolObservation.h"
#include "agent/ToolSelectionAgent.h"
#include "logging/Logger.h"
#include "permissions/ToolExecutionPolicy.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTypes.h"

#include <algorithm>
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
        logging::Logger& logger,
        AgentLoopConfig config)
        : selectionAgent_{ selectionAgent }
        , toolRegistry_{ toolRegistry }
        , executionPolicy_{ executionPolicy }
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

        AgentRunState state{
            .originalUserText = std::move(userText),
            .transientContext = std::move(initialTransientContext),
            .executedToolCount = 0,
            .executedRequestFingerprints = {}
        };

        return drive(
            std::move(state),
            std::nullopt);
    }


    AgentLoopResult AgentLoop::resumeConfirmed(
        PendingAgentRun pending)
    {
        return drive(
            std::move(pending.state),
            std::move(pending.confirmation.request));
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

                    appendTransientContext(
                        state.transientContext,
                        repeatedRequestGuard(request));

                    // false here means stop driving, but this is NOT a
                    // confirmation pause. The caller distinguishes using result.
                    result.status =
                        AgentLoopStatus::ReadyForResponse;

                    return false;
                }

                tools::ToolResult toolResult =
                    toolRegistry_.execute(
                        request);

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
            const AgentDecision decision =
                selectionAgent_.decide(
                    state.originalUserText,
                    state.transientContext);

            if (
                decision.action
                    == AgentAction::RespondNormally
                || !decision.toolRequest.has_value())
            {
                return finishReady();
            }

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
