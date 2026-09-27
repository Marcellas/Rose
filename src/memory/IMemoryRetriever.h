#pragma once

#include "memory/MemoryTypes.h"

#include <string_view>
#include <vector>

namespace rose::memory
{

    // Retrieval boundary used by RoseCore. RoseCore intentionally knows nothing
    // about lexical scoring, embeddings, vector databases, or storage layout.
    class IMemoryRetriever
    {
    public:
        virtual ~IMemoryRetriever() = default;

        [[nodiscard]]
        virtual std::vector<MemoryMatch> retrieve(
            std::string_view query,
            const MemoryRetrievalOptions& options = {}) const = 0;
    };

} // namespace rose::memory
