#pragma once

#include "tools/ITool.h"

namespace rose::process { class IProcessService; }
namespace rose::shortcuts { class IShortcutService; }

namespace rose::tools
{
    class LaunchProgramTool final : public ITool
    {
    public:
        LaunchProgramTool(
            process::IProcessService& processService,
            shortcuts::IShortcutService& shortcutService);

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        process::IProcessService& processService_;
        shortcuts::IShortcutService& shortcutService_;
        ToolDescriptor descriptor_;
    };
}
