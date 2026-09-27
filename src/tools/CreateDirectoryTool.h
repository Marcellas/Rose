#pragma once

#include "tools/ITool.h"

namespace rose::tools
{

    // Creates exactly one new directory.
    //
    // Safety properties:
    //   - absolute destination path required
    //   - parent directory must already exist
    //   - existing paths are never reused or replaced
    //   - ToolExecutionPolicy requires explicit confirmation
    class CreateDirectoryTool final : public ITool
    {
    public:
        CreateDirectoryTool();

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
