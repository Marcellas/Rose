#pragma once

#include "tools/ITool.h"

#include <cstddef>

namespace rose::tools
{

    struct ScanDirectoryTreeToolConfig
    {
        std::size_t maximumEntries{ 2000 };
        std::size_t maximumOutputBytes{ 6u * 1024u };
        std::size_t maximumDepth{ 8 };
    };


    // Recursively inventories a directory without opening file contents.
    // Symbolic links are listed but never traversed. The model-facing observation is
    // deliberately small enough to coexist with Rose's routing prompt; aggregate
    // counts still cover the bounded scan even when individual entry lines are cut.
    // This prevents recursive discovery from exhausting the local model context.
    class ScanDirectoryTreeTool final : public ITool
    {
    public:
        explicit ScanDirectoryTreeTool(
            ScanDirectoryTreeToolConfig config = {});

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        ScanDirectoryTreeToolConfig config_;
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
