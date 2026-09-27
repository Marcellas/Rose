#pragma once

#include "memory/MemoryTypes.h"

#include <vector>

namespace rose::memory
{

    // Persistent storage boundary for Rose long-term memory.
    //
    // This interface is deliberately record-oriented and provider-neutral. A
    // future SQLite/encrypted implementation can replace FileMemoryStore without
    // changing retrieval, tools, or RoseCore.
    class IMemoryStore
    {
    public:
        virtual ~IMemoryStore() = default;

        [[nodiscard]]
        virtual std::vector<MemoryRecord> loadMemories() = 0;

        virtual void appendMemory(
            const MemoryRecord& memory) = 0;
    };

} // namespace rose::memory
