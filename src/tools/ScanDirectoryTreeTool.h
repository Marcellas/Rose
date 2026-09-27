#pragma once

#include "tools/ITool.h"

#include <cstddef>

namespace rose::tools
{

    struct ScanDirectoryTreeToolConfig
    {
        std::size_t maximumEntries{ 2000 };
        std::size_t maximumOutputBytes{ 64u * 1024u };
        std::size_t maximumDepth{ 8 };
    };


    // Recursively inventories a directory without opening file contents.
    // Symbolic links are listed but never traversed. The observation is bounded
    // so very large trees can be discovered safely before a later batch-analysis
    // pipeline decides which files are worth reading.
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
