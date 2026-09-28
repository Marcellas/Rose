#include "agent/CodingTaskPlan.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace rose::agent
{
    namespace
    {
        [[nodiscard]]
        std::string trimCopy(
            const std::string_view text)
        {
            std::size_t first{ 0 };
            while (
                first < text.size()
                && std::isspace(
                    static_cast<unsigned char>(text[first])) != 0)
            {
                ++first;
            }

            std::size_t last = text.size();
            while (
                last > first
                && std::isspace(
                    static_cast<unsigned char>(text[last - 1u])) != 0)
            {
                --last;
            }

            return std::string{
                text.substr(first, last - first)
            };
        }


        [[nodiscard]]
        bool looksLikeAbsoluteWindowsPath(
            const std::string_view path) noexcept
        {
            if (path.size() >= 3)
            {
                const unsigned char drive =
                    static_cast<unsigned char>(path[0]);

                const bool asciiLetter =
                    (drive >= static_cast<unsigned char>('A')
                     && drive <= static_cast<unsigned char>('Z'))
                    || (drive >= static_cast<unsigned char>('a')
                        && drive <= static_cast<unsigned char>('z'));

                if (
                    asciiLetter
                    && path[1] == ':'
                    && (path[2] == '\\' || path[2] == '/'))
                {
                    return true;
                }
            }

            return path.starts_with("\\\\");
        }


        [[nodiscard]]
        std::string normalizedPath(
            const std::string_view rawPath)
        {
            std::string normalized =
                std::filesystem::path{ rawPath }
                    .lexically_normal()
                    .generic_string();

#ifdef _WIN32
            std::transform(
                normalized.begin(),
                normalized.end(),
                normalized.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(
                        std::tolower(character));
                });
#endif

            return normalized;
        }


        [[nodiscard]]
        std::string singleLine(
            const std::string_view value,
            const std::size_t maximumBytes)
        {
            std::string result;
            result.reserve((std::min)(value.size(), maximumBytes));

            for (const char character : value)
            {
                if (result.size() >= maximumBytes)
                {
                    break;
                }

                switch (character)
                {
                case '\r':
                case '\n':
                case '\t':
                    result.push_back(' ');
                    break;

                default:
                    if (
                        std::iscntrl(
                            static_cast<unsigned char>(character)) == 0)
                    {
                        result.push_back(character);
                    }
                    break;
                }
            }

            return result;
        }


        [[nodiscard]]
        std::optional<std::string_view> requestPath(
            const tools::ToolRequest& request) noexcept
        {
            const std::string_view argument =
                request.toolId == "reconfigure_cmake_project"
                    || request.toolId == "build_cmake_project"
                    || request.toolId == "run_cmake_tests"
                        ? std::string_view{ "source_path" }
                        : std::string_view{ "path" };

            const auto found =
                request.arguments.find(std::string{ argument });

            if (
                found == request.arguments.end()
                || found->second.empty())
            {
                return std::nullopt;
            }

            return found->second;
        }


        [[nodiscard]]
        bool sourceMutationRequest(
            const tools::ToolRequest& request) noexcept
        {
            return
                request.toolId == "edit_text_file"
                || request.toolId == "create_text_file";
        }
    } // namespace


    bool isSupportedCodingTaskPlanTool(
        const std::string_view toolId) noexcept
    {
        return
            toolId == "read_text_file"
            || toolId == "create_text_file"
            || toolId == "edit_text_file"
            || toolId == "reconfigure_cmake_project"
            || toolId == "build_cmake_project"
            || toolId == "run_cmake_tests";
    }


    std::optional<CodingTaskPlanStep> parseCodingTaskPlanStep(
        const std::string_view encoded)
    {
        const std::size_t firstSeparator =
            encoded.find('|');
        if (firstSeparator == std::string_view::npos)
        {
            return std::nullopt;
        }

        const std::size_t secondSeparator =
            encoded.find('|', firstSeparator + 1u);
        if (
            secondSeparator == std::string_view::npos
            || encoded.find('|', secondSeparator + 1u)
                != std::string_view::npos)
        {
            return std::nullopt;
        }

        CodingTaskPlanStep step{
            .toolId = trimCopy(encoded.substr(0, firstSeparator)),
            .path = trimCopy(
                encoded.substr(
                    firstSeparator + 1u,
                    secondSeparator - firstSeparator - 1u)),
            .note = trimCopy(encoded.substr(secondSeparator + 1u))
        };

        if (
            !isSupportedCodingTaskPlanTool(step.toolId)
            || !looksLikeAbsoluteWindowsPath(step.path)
            || step.note.empty()
            || step.note.size() > maximumCodingTaskPlanNoteBytes)
        {
            return std::nullopt;
        }

        return step;
    }


    bool isValidCodingTaskPlan(
        const CodingTaskPlan& plan)
    {
        if (
            plan.steps.size() < 2u
            || plan.steps.size() > maximumCodingTaskPlanSteps)
        {
            return false;
        }

        bool hasSourceMutation{ false };
        std::unordered_set<std::string> sourcePaths;
        sourcePaths.reserve(plan.steps.size());

        for (const CodingTaskPlanStep& step : plan.steps)
        {
            if (
                !isSupportedCodingTaskPlanTool(step.toolId)
                || !looksLikeAbsoluteWindowsPath(step.path)
                || step.note.empty()
                || step.note.size() > maximumCodingTaskPlanNoteBytes)
            {
                return false;
            }

            if (
                step.toolId == "read_text_file"
                || step.toolId == "create_text_file"
                || step.toolId == "edit_text_file")
            {
                sourcePaths.insert(
                    normalizedPath(step.path));
            }

            if (
                step.toolId == "create_text_file"
                || step.toolId == "edit_text_file")
            {
                hasSourceMutation = true;
            }
        }

        // ACTION=PLAN is specifically for coordinated multi-file coding work,
        // not a generic task list. Requiring two source paths plus one source
        // mutation prevents a read-only or single-file plan from satisfying the
        // AgentLoop's pre-write review gate.
        return
            hasSourceMutation
            && sourcePaths.size() >= 2u;
    }


    bool codingTaskPlanRequiredBeforeRequest(
        const CodingTaskWorkspaceState& workspace,
        const tools::ToolRequest& request)
    {
        if (!sourceMutationRequest(request))
        {
            return false;
        }

        std::unordered_set<std::string> distinctPaths;
        distinctPaths.reserve(workspace.sourceWindows.size());

        for (const tools::SourceWindowEvidence& evidence : workspace.sourceWindows)
        {
            if (!evidence.path.empty())
            {
                distinctPaths.insert(
                    normalizedPath(evidence.path));
            }
        }

        return distinctPaths.size() >= 2u;
    }


    bool codingTaskPlanCoversRequest(
        const CodingTaskPlan& plan,
        const tools::ToolRequest& request)
    {
        const std::optional<std::string_view> requestedPath =
            requestPath(request);

        if (!requestedPath.has_value())
        {
            return false;
        }

        const std::string requestedNormalized =
            normalizedPath(*requestedPath);

        return std::ranges::any_of(
            plan.steps,
            [&](const CodingTaskPlanStep& step)
            {
                return
                    step.toolId == request.toolId
                    && normalizedPath(step.path) == requestedNormalized;
            });
    }


    std::string formatCodingTaskPlanContext(
        const CodingTaskPlan& plan)
    {
        if (!isValidCodingTaskPlan(plan))
        {
            return {};
        }

        std::ostringstream text;
        text
            << "<rose_coding_task_plan>\n"
            << "authority=false\n"
            << "source=model_control_plan\n"
            << "step_count=" << plan.steps.size() << "\n";

        for (std::size_t index{ 0 }; index < plan.steps.size(); ++index)
        {
            const CodingTaskPlanStep& step = plan.steps[index];
            const std::size_t number = index + 1u;

            text
                << "step_" << number << "_tool="
                << step.toolId << "\n"
                << "step_" << number << "_path="
                << singleLine(step.path, 1024) << "\n"
                << "step_" << number << "_note="
                << singleLine(step.note, maximumCodingTaskPlanNoteBytes)
                << "\n";
        }

        text
            << "This plan is review context only. It does not authorize filesystem "
               "or process actions, and every exact ToolRequest must still pass its "
               "normal grounding, policy, provenance, and confirmation checks.\n"
            << "</rose_coding_task_plan>";

        return text.str();
    }


    std::string formatCodingTaskPlanForConfirmation(
        const CodingTaskPlan& plan,
        const tools::ToolRequest& currentRequest)
    {
        if (!isValidCodingTaskPlan(plan))
        {
            return {};
        }

        const std::optional<std::string_view> currentPath =
            requestPath(currentRequest);
        const std::string normalizedCurrent =
            currentPath.has_value()
                ? normalizedPath(*currentPath)
                : std::string{};

        bool matched{ false };
        std::ostringstream text;
        text
            << "Coding task plan (review only; each action keeps its normal safety checks):\n";

        for (std::size_t index{ 0 }; index < plan.steps.size(); ++index)
        {
            const CodingTaskPlanStep& step = plan.steps[index];
            const bool current =
                !matched
                && step.toolId == currentRequest.toolId
                && !normalizedCurrent.empty()
                && normalizedPath(step.path) == normalizedCurrent;

            if (current)
            {
                matched = true;
            }

            text
                << (current ? "> " : "  ")
                << (index + 1u)
                << ". "
                << step.toolId
                << " | "
                << singleLine(step.path, 800)
                << " | "
                << singleLine(step.note, maximumCodingTaskPlanNoteBytes)
                << "\n";
        }

        text
            << "- current_action_in_plan="
            << (matched ? "true" : "false")
            << "\n";

        if (!matched)
        {
            text
                << "- warning: the exact pending action is not listed in the advisory plan; "
                   "review the request parameters carefully before confirming.\n";
        }

        return text.str();
    }


    std::string buildCodingTaskPlanRequiredContext()
    {
        return
            "<rose_coding_plan_guard>\n"
            "reason=multi_file_write_requires_plan\n"
            "Rose has retained source evidence for multiple files in this bounded "
            "coding run. Before the first source mutation, create one explicit "
            "ACTION=PLAN with 2-8 grounded PLAN_STEP entries covering the intended "
            "read/edit/validation sequence. The plan is advisory and grants no tool "
            "authority. Do not execute or claim the blocked write completed.\n"
            "</rose_coding_plan_guard>";
    }

} // namespace rose::agent
