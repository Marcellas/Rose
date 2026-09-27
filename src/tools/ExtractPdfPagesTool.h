#pragma once

#include "documents/PdfDocumentMutationService.h"
#include "tools/ITool.h"

namespace rose::tools
{
    class ExtractPdfPagesTool final : public ITool
    {
    public:
        explicit ExtractPdfPagesTool(documents::IPdfDocumentMutationService& service);
        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        documents::IPdfDocumentMutationService& service_;
        ToolDescriptor descriptor_;
    };
}
