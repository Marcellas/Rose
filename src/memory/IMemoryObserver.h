#pragma once

#include <cstddef>
#include <string_view>

namespace rose::memory
{

    struct MemoryObservationResult
    {
        std::size_t examinedSentences{ 0 };
        std::size_t candidatesAdded{ 0 };
        std::size_t candidatesReinforced{ 0 };
        std::size_t candidatesPromoted{ 0 };
        std::size_t candidatesDiscardedAsDurable{ 0 };
    };


    // Provider-neutral hook for observing successful user turns after they have
    // been committed. Implementations may build temporary memory candidates or
    // perform other bounded local consolidation work.
    class IMemoryObserver
    {
    public:
        virtual ~IMemoryObserver() = default;

        [[nodiscard]]
        virtual MemoryObservationResult observeUserMessage(
            std::string_view userText) = 0;

        // Clears only session-scoped/temporary memory state. Durable memory is
        // owned by MemoryRepository/IMemoryStore and is deliberately unaffected.
        virtual void clearTemporaryMemory() = 0;
    };

} // namespace rose::memory
