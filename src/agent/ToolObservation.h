#pragma once

#include "tools/ToolTypes.h"

#include <string>

namespace rose::agent
{

    // Convert a completed tool execution into transient context for Rose's final
    // answer. This text is never persisted directly as the canonical user turn.
    [[nodiscard]]
    std::string buildToolObservation(
        const tools::ToolRequest& request,
        const tools::ToolResult& result);

} // namespace rose::agent
