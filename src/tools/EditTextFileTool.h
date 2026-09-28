#pragma once

#include "files/TextFileMutationService.h"
#include "tools/ITool.h"

namespace rose::tools
{
    class EditTextFileTool final : public ITool
    {
    public:
        explicit EditTextFileTool(
            files::ITextFileMutationService& mutationService);

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        files::ITextFileMutationService& mutationService_;
        ToolDescriptor descriptor_;
    };
}
