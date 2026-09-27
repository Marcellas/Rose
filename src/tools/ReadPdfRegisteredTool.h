#pragma once

#include "documents/ContextSafeDocumentSynthesizer.h"
#include "tools/ITool.h"
#include "tools/PdfTextExtractor.h"

#include <memory>

namespace rose::model { class IModelProvider; }
namespace rose::ocr { class IOcrEngine; }
namespace rose::permissions { class PermissionSystem; }

namespace rose::tools
{
    class ReadFileTool;

    class ReadPdfRegisteredTool final : public ITool
    {
    public:
        ReadPdfRegisteredTool(
            permissions::PermissionSystem& permissions,
            ReadFileTool& readFileTool,
            std::unique_ptr<ocr::IOcrEngine> ocrEngine,
            model::IModelProvider& modelProvider);
        ~ReadPdfRegisteredTool() override;

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        permissions::PermissionSystem& permissions_;
        ReadFileTool& readFileTool_;
        std::unique_ptr<ocr::IOcrEngine> ocrEngine_;
        PdfTextExtractor extractor_;
        documents::ContextSafeDocumentSynthesizer synthesizer_;
        ToolDescriptor descriptor_;
    };
}
