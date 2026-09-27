#pragma once

#include "memory/IMemoryStore.h"

#include <filesystem>

namespace rose::memory
{

    // Small append-only local memory journal for the v0.1 vertical slice.
    //
    // Properties:
    // - local-only by default
    // - one versioned length-prefixed record per memory
    // - incomplete final record is ignored after a crash
    // - persistence format is independent from the active language model
    //
    // This is intentionally replaceable. Once Rose needs mutation, large-scale
    // indexing, encryption-at-rest, or tens of thousands of records, SQLite is a
    // better backing store while this interface remains stable.
    class FileMemoryStore final : public IMemoryStore
    {
    public:
        explicit FileMemoryStore(
            std::filesystem::path path);

        [[nodiscard]]
        std::vector<MemoryRecord> loadMemories() override;

        void appendMemory(
            const MemoryRecord& memory) override;

        [[nodiscard]]
        const std::filesystem::path& path() const noexcept;

    private:
        std::filesystem::path path_;
    };

} // namespace rose::memory
