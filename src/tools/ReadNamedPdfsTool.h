#pragma once

#include "tools/ITool.h"

namespace rose::model { class IModelProvider; }

namespace rose::tools
{
    inline constexpr int maximumNamedPdfFiles{ 24 };

    // One confirmation covers only the exact PDF paths shown in this
    // request. Each read still uses the normal PDF extractor and OCR fallback.
    class ReadNamedPdfsTool final : public ITool
    {
    public:
        explicit ReadNamedPdfsTool(ITool& pdfReader,
            model::IModelProvider* modelProvider = nullptr);

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        ITool& pdfReader_;
        model::IModelProvider* modelProvider_;
        ToolDescriptor descriptor_;
    };
}
