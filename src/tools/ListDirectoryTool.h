#pragma once

#include "tools/ITool.h"

#include <cstddef>

namespace rose::tools
{

    struct ListDirectoryToolConfig
    {
        // Directory enumeration is intentionally shallow and bounded. Rose can
        // decide to inspect a child directory in a later Agent step instead of
        // recursively walking an entire drive in one request.
        std::size_t maximumEntries{ 200 };
        std::size_t maximumOutputBytes{ 16u * 1024u };
    };


    // Lists exactly one directory level.
    //
    // Safety properties:
    //   - absolute path required
    //   - no recursion
    //   - symbolic links are described, never followed
    //   - bounded entry count and output size
    //   - read-only
    //   - explicit confirmation required by ToolExecutionPolicy
    class ListDirectoryTool final : public ITool
    {
    public:
        explicit ListDirectoryTool(
            ListDirectoryToolConfig config = {});

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        ListDirectoryToolConfig config_;
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
