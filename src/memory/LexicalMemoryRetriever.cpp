#include "memory/LexicalMemoryRetriever.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rose::memory
{
    namespace
    {
        [[nodiscard]]
        bool isStopWord(
            const std::string_view word) noexcept
        {
            static constexpr std::string_view stopWords[]{
                "a", "about", "an", "and", "are", "as", "at", "be", "been",
                "but", "by", "can", "could", "did", "do", "does", "for", "from",
                "had", "has", "have", "he", "her", "hers", "him", "his", "how",
                "i", "if", "in", "into", "is", "it", "its", "me", "my", "of",
                "on", "or", "our", "ours", "say", "said", "she", "should", "tell",
                "that", "the", "their", "them", "there", "they", "this", "to", "was",
                "we", "were", "what", "when", "where", "which", "who", "why", "will"
            };

            return
                std::find(
                    std::begin(stopWords),
                    std::end(stopWords),
                    word)
                != std::end(stopWords);
        }


        [[nodiscard]]
        std::unordered_set<std::string> tokenize(
            const std::string_view text)
        {
            std::unordered_set<std::string> tokens;
            std::string current;

            const auto flush =
                [&]()
                {
                    if (
                        current.size() >= 2
                        && !isStopWord(current))
                    {
                        tokens.insert(current);
                    }

                    current.clear();
                };

            for (const unsigned char character : text)
            {
                if (std::isalnum(character) != 0)
                {
                    if (
                        character >= static_cast<unsigned char>('A')
                        && character <= static_cast<unsigned char>('Z'))
                    {
                        current.push_back(
                            static_cast<char>(
                                character
                                - static_cast<unsigned char>('A')
                                + static_cast<unsigned char>('a')));
                    }
                    else
                    {
                        current.push_back(
                            static_cast<char>(character));
                    }
                }
                else
                {
                    flush();
                }
            }

            flush();
            return tokens;
        }


        [[nodiscard]]
        double scoreMemory(
            const std::unordered_set<std::string>& queryTerms,
            const std::unordered_set<std::string>& memoryTerms)
        {
            if (
                queryTerms.empty()
                || memoryTerms.empty())
            {
                return 0.0;
            }

            std::size_t overlap{ 0 };

            for (const std::string& queryTerm : queryTerms)
            {
                if (memoryTerms.contains(queryTerm))
                {
                    ++overlap;
                }
            }

            if (overlap == 0)
            {
                return 0.0;
            }

            const double queryCoverage =
                static_cast<double>(overlap)
                / static_cast<double>(queryTerms.size());

            const double memoryCoverage =
                static_cast<double>(overlap)
                / static_cast<double>(memoryTerms.size());

            // Query coverage is intentionally dominant. A long detailed memory can
            // still be highly relevant to a short question even though most of the
            // memory's words do not appear in the question.
            return
                (queryCoverage * 0.85)
                + (memoryCoverage * 0.15);
        }
    }


    LexicalMemoryRetriever::LexicalMemoryRetriever(
        const MemoryRepository& repository)
        : repository_{ repository }
    {
    }


    std::vector<MemoryMatch> LexicalMemoryRetriever::retrieve(
        const std::string_view query,
        const MemoryRetrievalOptions& options) const
    {
        if (
            query.empty()
            || options.maximumResults == 0
            || options.maximumCombinedContentBytes == 0)
        {
            return {};
        }

        const auto queryTerms =
            tokenize(query);

        if (queryTerms.empty())
        {
            return {};
        }

        std::vector<MemoryMatch> candidates;

        for (const MemoryRecord* record : repository_.activeRecords())
        {
            const double score =
                scoreMemory(
                    queryTerms,
                    tokenize(record->content));

            if (score >= options.minimumScore)
            {
                candidates.push_back(
                    MemoryMatch{
                        .record = *record,
                        .score = score
                    });
            }
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const MemoryMatch& left, const MemoryMatch& right)
            {
                if (std::abs(left.score - right.score) > 0.000001)
                {
                    return left.score > right.score;
                }

                // Prefer newer memories when lexical relevance ties.
                return
                    left.record.unixTimeMilliseconds
                    > right.record.unixTimeMilliseconds;
            });

        std::vector<MemoryMatch> selected;
        selected.reserve(
            std::min(
                candidates.size(),
                options.maximumResults));

        std::size_t combinedBytes{ 0 };

        for (MemoryMatch& candidate : candidates)
        {
            if (selected.size() >= options.maximumResults)
            {
                break;
            }

            const std::size_t bytes =
                candidate.record.content.size();

            if (
                bytes > options.maximumCombinedContentBytes
                || combinedBytes
                    > options.maximumCombinedContentBytes - bytes)
            {
                continue;
            }

            combinedBytes += bytes;
            selected.push_back(
                std::move(candidate));
        }

        return selected;
    }

} // namespace rose::memory
