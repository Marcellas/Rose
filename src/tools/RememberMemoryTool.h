#pragma once

#include "memory/MemoryRepository.h"
#include "tools/ITool.h"

namespace rose::persistence { class IConversationStore; }

namespace rose::tools
{

    // Agent-facing adapter for explicit user memories.
    //
    // It borrows MemoryRepository; the repository owns the actual in-memory
    // records and its store owns persistence concerns.
    class RememberMemoryTool final : public ITool
    {
    public:
        explicit RememberMemoryTool(
            memory::MemoryRepository& repository,
            persistence::IConversationStore* conversationStore = nullptr);

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        memory::MemoryRepository& repository_;
        persistence::IConversationStore* conversationStore_;
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
