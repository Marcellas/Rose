#pragma once

#include "tools/ITool.h"

namespace rose::development
{
    class ICMakeConfigureService;
}

namespace rose::tools
{
    // Agent-facing adapter for the deliberately narrow existing-tree CMake
    // reconfiguration service. The adapter borrows the application-lifetime
    // service; ToolRegistry owns this tool instance.
    class ReconfigureCMakeProjectTool final : public ITool
    {
    public:
        explicit ReconfigureCMakeProjectTool(
            development::ICMakeConfigureService& service);

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        development::ICMakeConfigureService& service_;
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
