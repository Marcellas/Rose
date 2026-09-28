#include "agent/RepairValidationReplay.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rose::agent
{
    namespace
    {
        [[nodiscard]]
        bool hasGroundedSourceDiagnostic(
            const tools::ToolResult& result) noexcept
        {
            return
                !result.success
                && isSourceDiagnosticMetadata(result.trustedMetadata);
        }


        [[nodiscard]]
        std::string boundedSingleLine(
            const std::string& value,
            const std::size_t maximumCharacters = 512)
        {
            std::string result;
            result.reserve((std::min)(value.size(), maximumCharacters));

            for (const char character : value)
            {
                if (result.size() >= maximumCharacters)
                {
                    break;
                }

                if (character == '\r' || character == '\n')
                {
                    result.push_back(' ');
                }
                else
                {
                    result.push_back(character);
                }
            }

            if (value.size() > maximumCharacters)
            {
                result += "...";
            }

            return result;
        }
    } // namespace


    bool isDeveloperValidationRequest(
        const tools::ToolRequest& request) noexcept
    {
        return
            request.toolId == "build_cmake_project"
            || request.toolId == "reconfigure_cmake_project"
            || request.toolId == "run_cmake_tests";
    }


    void observeValidationResult(
        RepairValidationReplayState& state,
        const tools::ToolRequest& request,
        const tools::ToolResult& result)
    {
        if (!isDeveloperValidationRequest(request))
        {
            return;
        }

        // Any completed validation consumes a previously-ready replay. A fresh
        // failure may immediately replace it as the next repair candidate.
        state.ready = false;

        if (hasGroundedSourceDiagnostic(result))
        {
            state.failedValidationRequest = request;
            return;
        }

        // Success, timeout without a grounded source diagnostic, linker-only
        // failure, etc. cannot justify a source-bound automatic replay chain.
        state.failedValidationRequest.reset();
    }


    void observeCompletedRepair(
        RepairValidationReplayState& state,
        const SourceRepairPlan& plan,
        const tools::ToolResult& result)
    {
        state.ready = false;

        if (
            !result.success
            || !state.failedValidationRequest.has_value()
            || !plan.diagnostic.has_value())
        {
            return;
        }

        if (
            plan.diagnostic->producerTool
                != state.failedValidationRequest->toolId)
        {
            return;
        }

        state.ready = true;
    }


    std::optional<tools::ToolRequest> pendingRepairValidationRequest(
        const RepairValidationReplayState& state)
    {
        if (!state.ready || !state.failedValidationRequest.has_value())
        {
            return std::nullopt;
        }

        return state.failedValidationRequest;
    }


    std::string formatRepairValidationReplayMetadata(
        const RepairValidationReplayState& state)
    {
        const std::optional<tools::ToolRequest> request =
            pendingRepairValidationRequest(state);

        if (!request.has_value())
        {
            return {};
        }

        std::vector<std::pair<std::string, std::string>> arguments;
        arguments.reserve(request->arguments.size());
        for (const auto& [name, value] : request->arguments)
        {
            arguments.emplace_back(name, value);
        }
        std::sort(arguments.begin(), arguments.end());

        std::ostringstream text;
        text
            << "<rose_repair_validation_replay>\n"
            << "trusted=true\n"
            << "status=ready\n"
            << "tool_id=" << request->toolId << "\n"
            << "The last grounded source repair was motivated by this failed "
               "validation. Re-run this exact validation next to verify the repair; "
               "do not reconstruct or broaden it. Normal confirmation policy still applies.\n";

        for (const auto& [name, value] : arguments)
        {
            text
                << "argument_"
                << name
                << "="
                << boundedSingleLine(value)
                << "\n";
        }

        text << "</rose_repair_validation_replay>";
        return text.str();
    }

} // namespace rose::agent
