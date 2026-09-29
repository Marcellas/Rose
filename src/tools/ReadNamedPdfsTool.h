#pragma once

#include "tools/ITool.h"

namespace rose::tools
{
    // One confirmation covers only the 2-4 exact PDF paths shown in this
    // request. Each read still uses the normal PDF extractor and OCR fallback.
    class ReadNamedPdfsTool final : public ITool
    {
    public:
        explicit ReadNamedPdfsTool(ITool& pdfReader);

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        ITool& pdfReader_;
        ToolDescriptor descriptor_;
    };
}
