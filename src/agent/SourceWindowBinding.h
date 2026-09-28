#pragma once

#include "tools/ToolTypes.h"

#include <optional>

namespace rose::agent
{
    // Bind one proposed replace_line_range request to the immediately preceding
    // exact source-window observation when path/start/count match exactly.
    //
    // This helper is deliberately deterministic and model-independent. It never
    // invents replacement text or broadens a read. It only replaces model-echoed
    // preimage text with Rose-owned SHA-256 evidence for the exact same window.
    [[nodiscard]]
    tools::ToolRequest bindSourceWindowEvidence(
        const tools::ToolRequest& requested,
        const std::optional<tools::SourceWindowEvidence>& evidence);

} // namespace rose::agent
