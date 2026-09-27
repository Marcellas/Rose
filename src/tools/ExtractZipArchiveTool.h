#pragma once

#include "archives/ZipArchiveService.h"
#include "tools/ITool.h"

namespace rose::tools
{
    class ExtractZipArchiveTool final : public ITool
    {
    public:
        explicit ExtractZipArchiveTool(
            archives::IZipArchiveService& archiveService);

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        archives::IZipArchiveService& archiveService_;
        ToolDescriptor descriptor_;
    };
}
