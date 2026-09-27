#pragma once

#include "tools/ITool.h"

namespace rose::process { class IProcessService; }

namespace rose::tools
{
    class CloseProcessTool final : public ITool
    {
    public:
        explicit CloseProcessTool(process::IProcessService& processService);
        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;
    private:
        process::IProcessService& processService_;
        ToolDescriptor descriptor_;
    };
}
