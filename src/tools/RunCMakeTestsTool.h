#pragma once

#include "tools/ITool.h"

namespace rose::development { class ICMakeTestService; }

namespace rose::tools
{
    class RunCMakeTestsTool final : public ITool
    {
    public:
        explicit RunCMakeTestsTool(
            development::ICMakeTestService& service);

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        development::ICMakeTestService& service_;
        ToolDescriptor descriptor_;
    };
}
