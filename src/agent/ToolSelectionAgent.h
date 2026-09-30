#pragma once

#include "agent/AgentTypes.h"

#include <string_view>

namespace rose::logging
{
    class Logger;
}

namespace rose::model
{
    class IModelProvider;
}

namespace rose::tools
{
    class ToolRegistry;
}

namespace rose::agent
{

    // ToolSelectionAgent performs a small, non-persistent control inference
    // using Rose's already-loaded model provider.
    //
    // It does NOT execute tools. It only proposes either:
    //   - normal conversation, or
    //   - one ToolRequest.
    //
    // Ownership:
    //   ToolSelectionAgent borrows the model provider, registry, and logger.
    //   All three must outlive it.
    class ToolSelectionAgent final
    {
    public:
        ToolSelectionAgent(
            model::IModelProvider& modelProvider,
            const tools::ToolRegistry& toolRegistry,
            logging::Logger& logger);

        [[nodiscard]]
        AgentDecision decide(
            std::string_view userText,
            std::string_view agentContext = {},
            std::string_view trustedToolMetadata = {},
            std::string_view priorUserTaskContext = {}) const;

    private:
        [[nodiscard]]
        std::string buildSystemPrompt() const;

        // Focused first path for explicit new text/source creation. The normal
        // router carries the full tool catalog, while source creation may require a
        // comparatively large synthesized file body. Keeping synthesis in this
        // narrow prompt avoids spending the broad router's context window on code.
        // The returned request still passes normal grounding/permission checks.
        [[nodiscard]]
        AgentDecision recoverTextCreationDecision(
            std::string_view userText,
            std::string_view priorUserTaskContext,
            std::string_view requiredPath = {}) const;

        [[nodiscard]]
        AgentDecision parseDecision(
            std::string rawModelOutput) const;

        model::IModelProvider& modelProvider_;
        const tools::ToolRegistry& toolRegistry_;
        logging::Logger& logger_;
    };

} // namespace rose::agent
