#pragma once

#include "agent/CodingTaskWorkspace.h"
#include "tools/ToolTypes.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rose::agent
{
    inline constexpr std::size_t maximumCodingTaskPlanSteps{ 8 };
    inline constexpr std::size_t maximumCodingTaskPlanNoteBytes{ 160 };


    // One human-reviewable step in a bounded coding plan.
    //
    // IMPORTANT: this is advisory execution intent, not tool authority. The exact
    // ToolRequest still has to pass ToolSelectionAgent grounding, AgentLoop safety
    // checks, ToolExecutionPolicy, and any required user confirmation.
    struct CodingTaskPlanStep
    {
        std::string toolId;
        std::string path;
        std::string note;
    };


    struct CodingTaskPlan
    {
        std::vector<CodingTaskPlanStep> steps;
    };


    [[nodiscard]]
    bool isSupportedCodingTaskPlanTool(
        std::string_view toolId) noexcept;


    // Parse one model-control PLAN_STEP payload:
    //
    //   <tool_id>|<absolute Windows path>|<short note>
    //
    // The parser is intentionally strict and bounded. It does not establish any
    // filesystem authority; ToolSelectionAgent separately grounds every path.
    [[nodiscard]]
    std::optional<CodingTaskPlanStep> parseCodingTaskPlanStep(
        std::string_view encoded);


    [[nodiscard]]
    bool isValidCodingTaskPlan(
        const CodingTaskPlan& plan);


    // Multi-file writes are forced through an explicit plan once Rose has retained
    // valid source evidence for at least two distinct source paths in the current
    // bounded Agent run. A confirmation-resume never invents a new planning gate;
    // the gate is applied before the pending request is created.
    [[nodiscard]]
    bool codingTaskPlanRequiredBeforeRequest(
        const CodingTaskWorkspaceState& workspace,
        const tools::ToolRequest& request);


    [[nodiscard]]
    bool codingTaskPlanCoversRequest(
        const CodingTaskPlan& plan,
        const tools::ToolRequest& request);


    // Ordinary transient context returned to the control model after a plan is
    // accepted. It explicitly says the plan is model-produced and non-authoritative.
    [[nodiscard]]
    std::string formatCodingTaskPlanContext(
        const CodingTaskPlan& plan);


    // Human-facing confirmation preamble. The exact ToolRequest remains separately
    // visible and authoritative; this only shows the larger coding sequence the
    // user is being asked to review alongside it.
    [[nodiscard]]
    std::string formatCodingTaskPlanForConfirmation(
        const CodingTaskPlan& plan,
        const tools::ToolRequest& currentRequest);


    // Trusted guard text used when Rose has already inspected several source files
    // but the control model attempts the first write before producing a plan.
    [[nodiscard]]
    std::string buildCodingTaskPlanRequiredContext();

} // namespace rose::agent
