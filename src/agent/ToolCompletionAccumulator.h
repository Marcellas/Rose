#pragma once

#include "tools/ToolTypes.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rose::agent
{
    class ToolCompletionAccumulator final
    {
    public:
        void observe(const tools::ToolResult& result);

        [[nodiscard]]
        std::optional<std::string> authoritativeResponseIfComplete(
            std::size_t executedToolCount) const;

    private:
        bool requiresModelSynthesis_{ false };
        std::vector<std::string> authoritativeMessages_;
    };
}
