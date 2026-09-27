#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rose::memory
{

    struct NormalizedMemoryFact
    {
        // Stable slot-like key used only for conservative consolidation.
        // Examples:
        //   preference:coffee
        //   identity:user-name
        //   identity:location
        std::string key;

        // Lower-cased, punctuation-trimmed canonical value used to decide
        // whether two phrasings express the same value for the same key.
        std::string value;
    };


    // Deterministic, provider-independent normalization for a deliberately
    // small set of high-confidence memory facts. It is NOT intended to perform
    // open-ended semantic understanding. Unsupported statements simply return
    // nullopt and keep the existing append-only behavior.
    [[nodiscard]]
    std::optional<NormalizedMemoryFact> normalizeMemoryFact(
        std::string_view text);

} // namespace rose::memory
