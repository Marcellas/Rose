#pragma once

#include "tools/ITool.h"

namespace rose::development { class ICMakeBuildService; }

namespace rose::tools
{
    class BuildCMakeProjectTool final : public ITool
    {
    public:
        explicit BuildCMakeProjectTool(
            development::ICMakeBuildService& service);

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        development::ICMakeBuildService& service_;
        ToolDescriptor descriptor_;
    };
}
