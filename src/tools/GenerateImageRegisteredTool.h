#pragma once

#include "tools/GenerateImageTool.h"
#include "tools/ITool.h"

namespace rose::tools
{

    // Adapts the existing strongly-typed GenerateImageTool to the generic
    // ToolRegistry boundary.
    //
    // This class does not own GenerateImageTool; main's worker scope owns it.
    class GenerateImageRegisteredTool final : public ITool
    {
    public:
        explicit GenerateImageRegisteredTool(
            GenerateImageTool& generateImageTool);

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        GenerateImageTool& generateImageTool_;
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
