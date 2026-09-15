#pragma once

#include "tools/ITool.h"

#include <cstddef>

namespace rose::permissions
{
    class PermissionSystem;
}

namespace rose::tools
{
    class ReadFileTool;

    struct ReadTextFileRegisteredToolConfig
    {
        // Keep one Agent observation small enough that several reads can coexist
        // inside Qwen's current 8192-token context. ReadFileTool still enforces its
        // own larger hard safety limit before this adapter narrows the returned text.
        std::size_t maximumObservationBytes{ 8u * 1024u };
    };


    // Agent-facing adapter around the existing exact-file ReadFileTool primitive.
    //
    // ToolExecutionPolicy requires explicit confirmation for this descriptor. Once
    // the exact request is authorized, the adapter converts that authorization into
    // the one-shot grant already required by ReadFileTool. The grant is consumed by
    // the read before file contents are opened.
    //
    // This tool intentionally handles UTF-8 text/source files only. PDFs, images,
    // OCR, and semantic vision remain in the attachment/document pipeline for now.
    class ReadTextFileRegisteredTool final : public ITool
    {
    public:
        ReadTextFileRegisteredTool(
            permissions::PermissionSystem& permissions,
            ReadFileTool& readFileTool,
            ReadTextFileRegisteredToolConfig config = {});

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        permissions::PermissionSystem& permissions_;
        ReadFileTool& readFileTool_;
        ReadTextFileRegisteredToolConfig config_;
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
