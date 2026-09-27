#pragma once

#include "memory/IMemoryObserver.h"
#include "memory/MemoryRepository.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rose::memory
{

    struct ConversationMemoryCandidate
    {
        std::uint64_t id{ 0 };
        std::string content;
        std::size_t observationCount{ 0 };
        std::int64_t firstObservedUnixMilliseconds{ 0 };
        std::int64_t lastObservedUnixMilliseconds{ 0 };
    };


    struct ConversationMemoryCandidateConfig
    {
        // Repetition is the conservative auto-promotion signal for v0.1. A
        // single ordinary statement remains temporary and is never written to
        // long-term storage automatically.
        std::size_t promotionObservationCount{ 2 };
        std::size_t maximumCandidates{ 128 };
        std::size_t maximumCandidateBytes{ 512 };
        std::size_t maximumCandidatesPerMessage{ 4 };
    };


    // Conservative local-first conversation memory consolidator.
    //
    // Ownership/lifetime:
    // - borrows MemoryRepository for optional promotion to durable storage
    // - owns only bounded session-scoped candidates
    // - no model inference and no network access
    //
    // Automatic persistence requires repetition. This intentionally biases
    // Rose toward forgetting one-off chatter rather than permanently storing
    // an incorrect inference from a single sentence.
    class ConversationMemoryCandidateTracker final : public IMemoryObserver
    {
    public:
        explicit ConversationMemoryCandidateTracker(
            MemoryRepository& repository,
            ConversationMemoryCandidateConfig config = {});

        [[nodiscard]]
        MemoryObservationResult observeUserMessage(
            std::string_view userText) override;

        void clearTemporaryMemory() override;

        [[nodiscard]]
        const std::vector<ConversationMemoryCandidate>& candidates() const noexcept;

        [[nodiscard]]
        std::optional<RememberMemoryResult> promoteCandidate(
            std::uint64_t candidateId,
            std::string_view source = "manual-candidate-promotion");

    private:
        MemoryRepository& repository_;
        ConversationMemoryCandidateConfig config_;
        std::vector<ConversationMemoryCandidate> candidates_;
        std::uint64_t nextCandidateId_{ 1 };
    };

} // namespace rose::memory
