#pragma once

#include "tools/ITool.h"

#include <memory>

namespace rose::ocr { class IOcrEngine; }
namespace rose::permissions { class PermissionSystem; }
namespace rose::vision { class IVisionProvider; }

namespace rose::tools
{
    class ReadFileTool;

    class InspectImageRegisteredTool final : public ITool
    {
    public:
        InspectImageRegisteredTool(
            permissions::PermissionSystem& permissions,
            ReadFileTool& readFileTool,
            std::unique_ptr<ocr::IOcrEngine> ocrEngine,
            std::unique_ptr<vision::IVisionProvider> visionProvider);
        ~InspectImageRegisteredTool() override;

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        permissions::PermissionSystem& permissions_;
        ReadFileTool& readFileTool_;
        std::unique_ptr<ocr::IOcrEngine> ocrEngine_;
        std::unique_ptr<vision::IVisionProvider> visionProvider_;
        ToolDescriptor descriptor_;
    };
}
