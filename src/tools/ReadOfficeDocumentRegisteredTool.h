#pragma once

#include "documents/ContextSafeDocumentSynthesizer.h"
#include "documents/OpenXmlDocumentExtractor.h"
#include "tools/ITool.h"

namespace rose::model { class IModelProvider; }
namespace rose::permissions { class PermissionSystem; }

namespace rose::tools
{
    class ReadFileTool;

    class ReadOfficeDocumentRegisteredTool final : public ITool
    {
    public:
        ReadOfficeDocumentRegisteredTool(
            permissions::PermissionSystem& permissions,
            ReadFileTool& readFileTool,
            model::IModelProvider& modelProvider);

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        permissions::PermissionSystem& permissions_;
        ReadFileTool& readFileTool_;
        documents::OpenXmlDocumentExtractor extractor_;
        documents::ContextSafeDocumentSynthesizer synthesizer_;
        ToolDescriptor descriptor_;
    };
}
