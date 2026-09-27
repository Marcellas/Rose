#pragma once

#include "memory/IMemoryStore.h"
#include "memory/MemoryTypes.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace rose::memory
{

    // Owns Rose's currently loaded durable memory records while delegating disk
    // persistence to IMemoryStore.
    //
    // Lifetime/ownership:
    // - borrows IMemoryStore for the repository lifetime
    // - owns the in-memory MemoryRecord values
    // - callers receive copies or const references, never ownership of storage
    class MemoryRepository final
    {
    public:
        explicit MemoryRepository(
            IMemoryStore& store);

        [[nodiscard]]
        RememberMemoryResult remember(
            std::string_view content,
            MemoryKind kind,
            std::string_view source);

        [[nodiscard]]
        const std::vector<MemoryRecord>& records() const noexcept;

        [[nodiscard]]
        std::vector<const MemoryRecord*> activeRecords() const;

        [[nodiscard]]
        bool hasActiveEquivalent(std::string_view content) const;

        [[nodiscard]]
        std::size_t size() const noexcept;

        [[nodiscard]]
        std::size_t activeSize() const noexcept;

    private:
        IMemoryStore& store_;
        std::vector<MemoryRecord> records_;
        std::uint64_t nextId_{ 1 };
    };

} // namespace rose::memory
