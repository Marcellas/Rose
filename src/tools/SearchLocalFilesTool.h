#pragma once

#include "tools/ITool.h"

namespace rose::tools
{
    // A bounded, read-only search inside one user-selected directory. It searches
    // every filename and the contents of small UTF-8/plain-text files. Other file
    // types remain discoverable by name without attempting to parse binary data.
    class SearchLocalFilesTool final : public ITool
    {
    public:
        SearchLocalFilesTool();

        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        ToolDescriptor descriptor_;
    };
}
