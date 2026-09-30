#pragma once

#include "tools/ITool.h"

#include <string>

namespace rose::integrations { class IHttpClient; }

namespace rose::tools
{
    // Search snippets are evidence, never instructions or proof of page contents.
    class SearchOnlineTool final : public ITool
    {
    public:
        SearchOnlineTool(integrations::IHttpClient& http, std::string apiKey);
        [[nodiscard]] const ToolDescriptor& descriptor() const noexcept override;
        [[nodiscard]] ToolResult execute(const ToolRequest& request) override;

    private:
        integrations::IHttpClient& http_;
        std::string apiKey_;
        ToolDescriptor descriptor_;
    };
}
