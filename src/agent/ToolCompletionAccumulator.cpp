#include "agent/ToolCompletionAccumulator.h"

#include <sstream>

namespace rose::agent
{
    void ToolCompletionAccumulator::observe(
        const tools::ToolResult& result)
    {
        if (
            result.responseMode
            == tools::ToolResponseMode::RequiresModelSynthesis)
        {
            requiresModelSynthesis_ = true;
            return;
        }

        if (!result.message.empty())
        {
            authoritativeMessages_.push_back(result.message);
        }
    }


    std::optional<std::string>
    ToolCompletionAccumulator::authoritativeResponseIfComplete(
        const std::size_t executedToolCount) const
    {
        if (
            executedToolCount == 0
            || requiresModelSynthesis_
            || authoritativeMessages_.empty())
        {
            return std::nullopt;
        }

        std::ostringstream text;

        for (std::size_t i = 0; i < authoritativeMessages_.size(); ++i)
        {
            if (i > 0)
            {
                text << '\n';
            }

            text << authoritativeMessages_[i];
        }

        return text.str();
    }
}
