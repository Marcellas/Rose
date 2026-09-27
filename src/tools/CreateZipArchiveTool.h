#pragma once

#include "archives/ZipArchiveService.h"
#include "tools/ITool.h"

namespace rose::tools
{
    class CreateZipArchiveTool final : public ITool
    {
    public:
        explicit CreateZipArchiveTool(
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
