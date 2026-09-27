#pragma once

#include "documents/OfficeDocumentMutationService.h"
#include "tools/ITool.h"

namespace rose::tools
{
    class EditOfficeDocumentTool final : public ITool
    {
    public:
        explicit EditOfficeDocumentTool(
            documents::IOfficeDocumentMutationService& mutationService);

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        documents::IOfficeDocumentMutationService& mutationService_;
        ToolDescriptor descriptor_;
    };
}
