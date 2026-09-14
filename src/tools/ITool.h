#pragma once

#include "tools/ToolTypes.h"

namespace rose::tools
{

    class ITool
    {
    public:
        virtual ~ITool() = default;

        [[nodiscard]]
        virtual const ToolDescriptor& descriptor() const noexcept = 0;

        [[nodiscard]]
        virtual ToolResult execute(
            const ToolRequest& request) = 0;
    };

} // namespace rose::tools
