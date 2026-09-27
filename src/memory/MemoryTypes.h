#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rose::memory
{

    // Distinguishes the major kinds of durable knowledge Rose may eventually
    // retrieve. The v0.1 write path currently creates ExplicitUser records, but
    // defining the broader taxonomy now keeps persistence/provider code from
    // assuming that every future memory came from the same source.
    enum class MemoryKind : std::uint32_t
    {
        ExplicitUser = 1,
        ConversationDerived = 2,
        ProjectKnowledge = 3,
        DocumentSource = 4
    };


    struct MemoryRecord
    {
        std::uint64_t id{ 0 };
        MemoryKind kind{ MemoryKind::ExplicitUser };
        std::int64_t unixTimeMilliseconds{ 0 };

        // Provider-neutral provenance. Examples:
        //     explicit-user-request
        //     conversation-extraction
        //     document:C:\...\file.pdf
        std::string source;
        std::string content;

        // Session-derived semantic consolidation metadata. These fields are
        // intentionally NOT part of the v0.1 append-only journal format; they
        // are rebuilt deterministically from content whenever the repository
        // loads. That keeps existing .rosemem files backward compatible.
        std::string semanticKey;
        std::string semanticValue;
        std::uint64_t supersededById{ 0 };

        [[nodiscard]]
        bool active() const noexcept
        {
            return supersededById == 0;
        }
    };


    struct RememberMemoryResult
    {
        MemoryRecord record;
        bool created{ false };
    };


    struct MemoryMatch
    {
        MemoryRecord record;
        double score{ 0.0 };
    };


    struct MemoryRetrievalOptions
    {
        std::size_t maximumResults{ 5 };
        std::size_t maximumCombinedContentBytes{ 4096 };
        double minimumScore{ 0.20 };
    };


    [[nodiscard]]
    inline const char* toString(
        const MemoryKind kind) noexcept
    {
        switch (kind)
        {
        case MemoryKind::ExplicitUser:
            return "explicit-user";

        case MemoryKind::ConversationDerived:
            return "conversation-derived";

        case MemoryKind::ProjectKnowledge:
            return "project-knowledge";

        case MemoryKind::DocumentSource:
            return "document-source";
        }

        return "unknown";
    }

} // namespace rose::memory
