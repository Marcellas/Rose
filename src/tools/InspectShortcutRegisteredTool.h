#pragma once

#include "tools/ITool.h"

namespace rose::permissions { class PermissionSystem; }
namespace rose::shortcuts { class IShortcutService; }

namespace rose::tools
{
    class InspectShortcutRegisteredTool final : public ITool
    {
    public:
        InspectShortcutRegisteredTool(
            permissions::PermissionSystem& permissions,
            shortcuts::IShortcutService& shortcutService);

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        permissions::PermissionSystem& permissions_;
        shortcuts::IShortcutService& shortcutService_;
        ToolDescriptor descriptor_;
    };
}
