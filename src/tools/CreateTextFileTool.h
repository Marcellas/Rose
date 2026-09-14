#pragma once

#include "tools/ITool.h"

namespace rose::tools
{

    // CreateTextFileTool creates exactly one NEW UTF-8 text file.
    //
    // Safety properties for this first write-capable tool:
    //   - absolute destination path required
    //   - parent directory must already exist
    //   - existing files are never overwritten
    //   - executable/script extensions are not accepted
    //   - content is bounded to 1 MiB
    //   - ToolExecutionPolicy requires explicit confirmation
    //
    // On Windows the actual file creation uses CREATE_NEW, so the no-overwrite
    // guarantee is enforced atomically by the OS rather than by a check-then-open
    // sequence.
    class CreateTextFileTool final : public ITool
    {
    public:
        CreateTextFileTool();

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
