#include "memory/MemoryRepository.h"

#include "memory/MemoryFactNormalizer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace rose::memory
{
    namespace
    {
        [[nodiscard]]
        std::string_view trimAsciiWhitespace(
            const std::string_view text) noexcept
        {
            constexpr std::string_view whitespace{ " \t\r\n" };

            const std::size_t first = text.find_first_not_of(whitespace);
            if (first == std::string_view::npos)
            {
                return {};
            }

            const std::size_t last = text.find_last_not_of(whitespace);
            return text.substr(first, last - first + 1);
        }


        [[nodiscard]]
        std::string asciiLower(
            const std::string_view text)
        {
            std::string lowered;
            lowered.reserve(text.size());

            for (const unsigned char character : text)
            {
                if (
                    character >= static_cast<unsigned char>('A')
                    && character <= static_cast<unsigned char>('Z'))
                {
                    lowered.push_back(static_cast<char>(
                        character - static_cast<unsigned char>('A')
                        + static_cast<unsigned char>('a')));
                }
                else
                {
                    lowered.push_back(static_cast<char>(character));
                }
            }

            return lowered;
        }


        [[nodiscard]]
        std::int64_t currentUnixMilliseconds()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }


        void decorateSemanticState(
            std::vector<MemoryRecord>& records)
        {
            std::unordered_map<std::string, std::size_t> newestByKey;

            for (std::size_t index = 0; index < records.size(); ++index)
            {
                MemoryRecord& record = records[index];
                record.semanticKey.clear();
                record.semanticValue.clear();
                record.supersededById = 0;

                const auto fact = normalizeMemoryFact(record.content);
                if (!fact.has_value())
                {
                    continue;
                }

                record.semanticKey = fact->key;
                record.semanticValue = fact->value;

                const auto previous = newestByKey.find(record.semanticKey);
                if (previous != newestByKey.end())
                {
                    records[previous->second].supersededById = record.id;
                }

                newestByKey.insert_or_assign(record.semanticKey, index);
            }
        }
    }


    MemoryRepository::MemoryRepository(
        IMemoryStore& store)
        : store_{ store }
        , records_{ store_.loadMemories() }
    {
        decorateSemanticState(records_);

        std::uint64_t maximumId{ 0 };

        for (const MemoryRecord& record : records_)
        {
            maximumId = std::max(maximumId, record.id);
        }

        if (maximumId == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::runtime_error{
                "Rose memory id space is exhausted."
            };
        }

        nextId_ = maximumId + 1;
    }


    RememberMemoryResult MemoryRepository::remember(
        const std::string_view content,
        const MemoryKind kind,
        const std::string_view source)
    {
        const std::string_view trimmed = trimAsciiWhitespace(content);

        if (trimmed.empty())
        {
            throw std::invalid_argument{
                "Rose cannot remember empty content."
            };
        }

        const std::string normalized = asciiLower(trimmed);
        const auto newFact = normalizeMemoryFact(trimmed);

        if (newFact.has_value())
        {
            for (const MemoryRecord& existing : records_)
            {
                if (
                    !existing.active()
                    || existing.semanticKey != newFact->key
                    || existing.semanticValue != newFact->value)
                {
                    continue;
                }

                // An explicit user instruction is allowed to upgrade a prior
                // conversation-derived fact even when the value is unchanged.
                // Otherwise the already-active semantic equivalent wins.
                if (
                    kind != MemoryKind::ExplicitUser
                    || existing.kind == MemoryKind::ExplicitUser)
                {
                    return RememberMemoryResult{
                        .record = existing,
                        .created = false
                    };
                }
            }
        }
        else
        {
            // Preserve the original exact duplicate behavior for facts outside
            // the conservative semantic normalizer.
            for (const MemoryRecord& existing : records_)
            {
                if (
                    existing.active()
                    && existing.kind == kind
                    && asciiLower(trimAsciiWhitespace(existing.content))
                        == normalized)
                {
                    return RememberMemoryResult{
                        .record = existing,
                        .created = false
                    };
                }
            }
        }

        if (nextId_ == 0)
        {
            throw std::runtime_error{
                "Rose memory id space is exhausted."
            };
        }

        MemoryRecord record{
            .id = nextId_,
            .kind = kind,
            .unixTimeMilliseconds = currentUnixMilliseconds(),
            .source = std::string{ source },
            .content = std::string{ trimmed },
            .semanticKey = {},
            .semanticValue = {},
            .supersededById = 0
        };

        if (newFact.has_value())
        {
            record.semanticKey = newFact->key;
            record.semanticValue = newFact->value;
        }

        // Reserve before persistence. Once the disk write succeeds, push_back()
        // cannot fail due to vector reallocation, keeping disk and memory aligned.
        records_.reserve(records_.size() + 1);
        store_.appendMemory(record);

        if (!record.semanticKey.empty())
        {
            for (MemoryRecord& existing : records_)
            {
                if (
                    existing.active()
                    && existing.semanticKey == record.semanticKey)
                {
                    existing.supersededById = record.id;
                }
            }
        }

        records_.push_back(record);

        if (nextId_ == std::numeric_limits<std::uint64_t>::max())
        {
            nextId_ = 0;
        }
        else
        {
            ++nextId_;
        }

        return RememberMemoryResult{
            .record = std::move(record),
            .created = true
        };
    }


    const std::vector<MemoryRecord>&
        MemoryRepository::records() const noexcept
    {
        return records_;
    }


    std::vector<const MemoryRecord*>
        MemoryRepository::activeRecords() const
    {
        std::vector<const MemoryRecord*> active;
        active.reserve(records_.size());

        for (const MemoryRecord& record : records_)
        {
            if (record.active())
            {
                active.push_back(&record);
            }
        }

        return active;
    }


    bool MemoryRepository::hasActiveEquivalent(
        const std::string_view content) const
    {
        const auto candidateFact = normalizeMemoryFact(content);

        if (candidateFact.has_value())
        {
            for (const MemoryRecord& record : records_)
            {
                if (
                    record.active()
                    && record.semanticKey == candidateFact->key
                    && record.semanticValue == candidateFact->value)
                {
                    return true;
                }
            }

            return false;
        }

        const std::string normalized =
            asciiLower(trimAsciiWhitespace(content));

        for (const MemoryRecord& record : records_)
        {
            if (
                record.active()
                && asciiLower(trimAsciiWhitespace(record.content))
                    == normalized)
            {
                return true;
            }
        }

        return false;
    }


    std::size_t MemoryRepository::size() const noexcept
    {
        return records_.size();
    }


    std::size_t MemoryRepository::activeSize() const noexcept
    {
        return static_cast<std::size_t>(
            std::count_if(
                records_.begin(),
                records_.end(),
                [](const MemoryRecord& record)
                {
                    return record.active();
                }));
    }

} // namespace rose::memory
