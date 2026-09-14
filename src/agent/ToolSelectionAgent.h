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
            std::string_view agentContext = {}) const;

    private:
        [[nodiscard]]
        std::string buildSystemPrompt() const;

        [[nodiscard]]
        AgentDecision parseDecision(
            std::string rawModelOutput) const;

        model::IModelProvider& modelProvider_;
        const tools::ToolRegistry& toolRegistry_;
        logging::Logger& logger_;
    };

} // namespace rose::agent
