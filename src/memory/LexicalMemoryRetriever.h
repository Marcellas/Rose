#pragma once

#include "memory/IMemoryRetriever.h"
#include "memory/MemoryRepository.h"

namespace rose::memory
{

    // Lightweight offline retriever for Rose v0.1.
    //
    // It deliberately favors query-term coverage over full-document similarity,
    // because explicit memories are often sentence-sized while the user's question
    // is short. This is a replaceable baseline, not the final semantic retriever.
    class LexicalMemoryRetriever final : public IMemoryRetriever
    {
    public:
        explicit LexicalMemoryRetriever(
            const MemoryRepository& repository);

        [[nodiscard]]
        std::vector<MemoryMatch> retrieve(
            std::string_view query,
            const MemoryRetrievalOptions& options = {}) const override;

    private:
        const MemoryRepository& repository_;
    };

} // namespace rose::memory
