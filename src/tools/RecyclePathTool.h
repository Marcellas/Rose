#pragma once

#include "tools/ITool.h"

namespace rose::tools
{

    // Sends one existing file or directory to the operating-system recycle bin.
    // This is classified Destructive so ToolExecutionPolicy always requires an
    // explicit confirmation for the exact pending request.
    class RecyclePathTool final : public ITool
    {
    public:
        RecyclePathTool();

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
