#pragma once

#include "archives/ZipArchiveService.h"
#include "tools/ITool.h"

#include <cstddef>

namespace rose::tools
{
    struct ListZipArchiveToolConfig
    {
        std::size_t maximumOutputBytes{ 32u * 1024u };
    };

    class ListZipArchiveTool final : public ITool
    {
    public:
        explicit ListZipArchiveTool(
            archives::IZipArchiveService& archiveService,
            ListZipArchiveToolConfig config = {});

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        archives::IZipArchiveService& archiveService_;
        ListZipArchiveToolConfig config_;
        ToolDescriptor descriptor_;
    };
}
