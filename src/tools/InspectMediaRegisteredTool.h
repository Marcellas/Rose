#pragma once

#include "tools/ITool.h"

#include <memory>

namespace rose::media { class IMediaService; }
namespace rose::permissions { class PermissionSystem; }
namespace rose::vision { class IVisionProvider; }

namespace rose::tools
{
    class InspectMediaRegisteredTool final : public ITool
    {
    public:
        InspectMediaRegisteredTool(
            permissions::PermissionSystem& permissions,
            media::IMediaService& mediaService,
            std::unique_ptr<vision::IVisionProvider> visionProvider);
        ~InspectMediaRegisteredTool() override;

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        permissions::PermissionSystem& permissions_;
        media::IMediaService& mediaService_;
        std::unique_ptr<vision::IVisionProvider> visionProvider_;
        ToolDescriptor descriptor_;
    };
}
