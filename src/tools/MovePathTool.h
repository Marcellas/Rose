#pragma once

#include "tools/ITool.h"

namespace rose::tools
{

    // Moves or renames one existing filesystem entry without overwriting the
    // destination. std::filesystem::rename keeps the operation simple and
    // atomic when the platform/filesystem supports it.
    class MovePathTool final : public ITool
    {
    public:
        MovePathTool();

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
