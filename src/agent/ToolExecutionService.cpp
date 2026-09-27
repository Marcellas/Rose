#include "agent/ToolExecutionService.h"

#include "agent/AgentJournal.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"

#include <chrono>
#include <exception>
#include <stdexcept>

namespace rose::agent
{
    ToolExecutionService::ToolExecutionService(
        tools::ToolRegistry& registry,
        permissions::ToolExecutionPolicy& policy,
        AgentJournal& journal)
        : registry_{ registry }
        , policy_{ policy }
        , journal_{ journal }
    {
    }

    ToolExecutionServiceResult ToolExecutionService::execute(
        const tools::ToolRequest& request,
        const std::uint64_t runId,
        const std::size_t stepIndex,
        const permissions::ToolConfirmationState confirmation)
    {
        const tools::ITool* tool = registry_.find(request.toolId);
        if (tool == nullptr)
        {
            const std::string reason =
                "No registered Rose tool has id '"
                + request.toolId
                + "'.";

            journal_.recordToolRequest(
                runId,
                AgentEventType::ToolFailed,
                stepIndex,
                request,
                reason);

            throw std::runtime_error{ reason };
        }

        ToolExecutionServiceResult outcome;
        outcome.descriptor = tool->descriptor();

        const permissions::ToolExecutionDecision decision =
            policy_.evaluate(
                outcome.descriptor,
                confirmation);
        outcome.reason = decision.reason;

        if (decision.disposition
            == permissions::ToolExecutionDisposition::RequiresConfirmation)
        {
            journal_.recordToolRequest(
                runId,
                AgentEventType::ConfirmationRequired,
                stepIndex,
                request,
                decision.reason);
            outcome.status = ToolExecutionServiceStatus::RequiresConfirmation;
            return outcome;
        }

        if (!decision.allowed())
        {
            journal_.recordToolRequest(
                runId,
                AgentEventType::PolicyDenied,
                stepIndex,
                request,
                decision.reason);
            outcome.status = ToolExecutionServiceStatus::Denied;
            return outcome;
        }

        journal_.recordToolRequest(
            runId,
            AgentEventType::ToolStarted,
            stepIndex,
            request,
            "Tool execution started.");

        const auto startedAt = std::chrono::steady_clock::now();

        try
        {
            outcome.result = registry_.execute(request);
        }
        catch (const std::exception& exception)
        {
            const auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startedAt);
            journal_.recordToolRequest(
                runId,
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
                    std::chrono::steady_clock::now() - startedAt);
            journal_.recordToolRequest(
                runId,
                AgentEventType::ToolFailed,
                stepIndex,
                request,
                "Tool execution failed with an unknown exception.",
                duration);
            throw;
        }

        const auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startedAt);

        journal_.recordToolRequest(
            runId,
            AgentEventType::ToolFinished,
            stepIndex,
            request,
            outcome.result.message.empty()
                ? "Tool execution completed."
                : outcome.result.message,
            duration);

        outcome.status = ToolExecutionServiceStatus::Completed;
        return outcome;
    }
}
