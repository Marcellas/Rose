#pragma once

#include "tools/ITool.h"

namespace rose::database { class IDatabaseService; }
namespace rose::permissions { class PermissionSystem; }

namespace rose::tools
{
    class InspectDatabaseRegisteredTool final : public ITool
    {
    public:
        InspectDatabaseRegisteredTool(
            permissions::PermissionSystem& permissions,
            database::IDatabaseService& databaseService);

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        permissions::PermissionSystem& permissions_;
        database::IDatabaseService& databaseService_;
        ToolDescriptor descriptor_;
    };
}
