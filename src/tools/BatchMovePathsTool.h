#pragma once

#include "tools/ITool.h"

#include <cstddef>

namespace rose::tools
{

    struct BatchMovePathsToolConfig
    {
        std::size_t maximumOperations{ 64 };
    };


    // Confirmation-gated batch move/rename primitive.
    //
    // The entire operation list is preflighted before the first filesystem
    // mutation. Existing destinations are never overwritten. If a later rename
    // fails, Rose attempts to roll back already-completed moves in reverse order.
    class BatchMovePathsTool final : public ITool
    {
    public:
        explicit BatchMovePathsTool(
            BatchMovePathsToolConfig config = {});

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        BatchMovePathsToolConfig config_;
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
