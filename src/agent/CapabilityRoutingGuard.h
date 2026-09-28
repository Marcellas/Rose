#pragma once

#include "tools/ToolTypes.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>


namespace rose::tools
{
    class ToolRegistry;
}


namespace rose::agent
{

    // -------------------------------------------------------------------------
    // CapabilityRoutingGuard
    // -------------------------------------------------------------------------
    //
    // Small deterministic support layer around model-driven tool selection.
    //
    // It does NOT choose or execute tools.
    //
    // Its responsibilities are deliberately narrower:
    //
    // 1. Build an authoritative, transient capability contract from the LIVE
    //    ToolRegistry so Rose does not hallucinate that registered tools are absent.
    //
    // 2. Detect strong evidence that a model-selected "respond normally" decision
    //    deserves ONE bounded second look.
    //
    // 3. Build the transient guard text for that one re-check.
    //
    // ToolSelectionAgent still chooses the tool request.
    // ToolExecutionPolicy still decides whether it may execute.
    // ToolRegistry still owns actual execution.
    class CapabilityRoutingGuard final
    {
    public:
        [[nodiscard]]
        static std::string buildCapabilityContract(
            const tools::ToolRegistry& toolRegistry);


        [[nodiscard]]
        static bool likelyToolBackedRequest(
            std::string_view userText,
            const tools::ToolRegistry& toolRegistry);


        [[nodiscard]]
        static std::string buildRecheckGuard(
            std::size_t completedToolCount);


        // Shared deterministic classifier for explicit CTest execution intent.
        // ToolSelectionAgent and direct-request recovery both use this so the
        // model-validation path cannot drift from the fallback recovery path.
        [[nodiscard]]
        static bool explicitCMakeTestExecutionIntent(
            std::string_view userText);


        // Recover only narrow, structurally obvious requests after BOTH model
        // routing passes declined to invoke a tool.
        //
        // Recovery still returns an ordinary ToolRequest. AgentLoop sends it
        // through ToolExecutionPolicy and ToolRegistry exactly like a model-
        // proposed request.
        [[nodiscard]]
        static std::optional<tools::ToolRequest>
        recoverDirectToolRequest(
            std::string_view userText,
            const tools::ToolRegistry& toolRegistry,
            std::span<const std::string_view> completedToolIds,
            std::string_view agentContext = {});


        // When a confirmed configure/build/test validation fails during an explicit repair/debug
        // workflow, recover one narrow read_text_file window from Rose-owned
        // trusted diagnostic metadata. Raw configure/compiler/test output is never parsed
        // here and therefore cannot manufacture arbitrary read authority.
        [[nodiscard]]
        static std::optional<tools::ToolRequest>
        recoverDiagnosticSourceReadRequest(
            std::string_view userText,
            const tools::ToolRegistry& toolRegistry,
            std::string_view trustedToolMetadata);


        // Final-response evidence guard. This is appended whenever a request still
        // falls back to normal conversation after being recognized as tool-like.
        // It prevents the final model from turning capability availability into a
        // false claim that execution actually occurred.
        [[nodiscard]]
        static std::string buildExecutionEvidenceGuard(
            std::size_t completedToolCount);
    };

} // namespace rose::agent
