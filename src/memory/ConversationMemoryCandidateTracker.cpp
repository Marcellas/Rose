#include "memory/ConversationMemoryCandidateTracker.h"
#include "memory/MemoryFactNormalizer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_set>
#include <vector>

namespace rose::memory
{
    namespace
    {
        [[nodiscard]]
        std::string_view trimAsciiWhitespace(
            const std::string_view text) noexcept
        {
            constexpr std::string_view whitespace{
                " \t\r\n"
            };

            const std::size_t first =
                text.find_first_not_of(whitespace);

            if (first == std::string_view::npos)
            {
                return {};
            }

            const std::size_t last =
                text.find_last_not_of(whitespace);

            return text.substr(
                first,
                last - first + 1);
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
                    lowered.push_back(
                        static_cast<char>(
                            character
                            - static_cast<unsigned char>('A')
                            + static_cast<unsigned char>('a')));
                }
                else
                {
                    lowered.push_back(
                        static_cast<char>(character));
                }
            }

            return lowered;
        }


        [[nodiscard]]
        std::string normalizedCandidateText(
            const std::string_view text)
        {
            std::string normalized;
            normalized.reserve(text.size());

            bool previousWhitespace{ false };

            for (const unsigned char raw : trimAsciiWhitespace(text))
            {
                const bool whitespace =
                    std::isspace(raw) != 0;

                if (whitespace)
                {
                    if (!previousWhitespace && !normalized.empty())
                    {
                        normalized.push_back(' ');
                    }

                    previousWhitespace = true;
                    continue;
                }

                previousWhitespace = false;

                unsigned char character = raw;
                if (character >= 'A' && character <= 'Z')
                {
                    character =
                        static_cast<unsigned char>(
                            character - 'A' + 'a');
                }

                normalized.push_back(
                    static_cast<char>(character));
            }

            while (
                !normalized.empty()
                && (
                    normalized.back() == '.'
                    || normalized.back() == '!'
                    || normalized.back() == '?'))
            {
                normalized.pop_back();
            }

            return normalized;
        }




        [[nodiscard]]
        std::string candidateIdentity(
            const std::string_view text)
        {
            if (const auto fact = normalizeMemoryFact(text); fact.has_value())
            {
                return
                    "fact:"
                    + fact->key
                    + "="
                    + fact->value;
            }

            return normalizedCandidateText(text);
        }

        [[nodiscard]]
        std::int64_t currentUnixMilliseconds()
        {
            return
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                .count();
        }


        [[nodiscard]]
        bool startsWithAsciiCaseInsensitive(
            const std::string_view text,
            const std::string_view prefix) noexcept
        {
            if (text.size() < prefix.size())
            {
                return false;
            }

            for (std::size_t index = 0;
                 index < prefix.size();
                 ++index)
            {
                char left = text[index];
                char right = prefix[index];

                if (left >= 'A' && left <= 'Z')
                {
                    left = static_cast<char>(left - 'A' + 'a');
                }

                if (right >= 'A' && right <= 'Z')
                {
                    right = static_cast<char>(right - 'A' + 'a');
                }

                if (left != right)
                {
                    return false;
                }
            }

            return true;
        }


        [[nodiscard]]
        bool looksLikeExplicitMemoryCommand(
            const std::string_view text) noexcept
        {
            static constexpr std::string_view prefixes[]{
                "remember ",
                "remember that ",
                "please remember ",
                "please remember that ",
                "save this memory",
                "store this memory"
            };

            for (const std::string_view prefix : prefixes)
            {
                if (startsWithAsciiCaseInsensitive(text, prefix))
                {
                    return true;
                }
            }

            return false;
        }


        [[nodiscard]]
        bool containsStableCue(
            const std::string_view lowerText)
        {
            // Conservative cues only. These are intentionally phrases that usually
            // express preferences, stable identity/context, persistent workflow,
            // or project design intent. Broader semantic extraction can replace
            // this component later without changing repository/storage APIs.
            static constexpr std::string_view cues[]{
                "i prefer ",
                "i like ",
                "i love ",
                "i dislike ",
                "i hate ",
                "my favorite ",
                "my favourite ",
                "my name is ",
                "call me ",
                "i live in ",
                "i am based in ",
                "i'm based in ",
                "my timezone is ",
                "my time zone is ",
                "i use ",
                "i work with ",
                "i work on ",
                "i am building ",
                "i'm building ",
                "i am working on ",
                "i'm working on ",
                "rose should ",
                "rose must ",
                "rose needs to "
            };

            for (const std::string_view cue : cues)
            {
                if (lowerText.find(cue) != std::string_view::npos)
                {
                    return true;
                }
            }

            // Common relationship/name facts are useful but kept narrow to avoid
            // turning generic "my X is Y" statements into durable guesses.
            static constexpr std::string_view relationCues[]{
                "my dog's name is ",
                "my cat's name is ",
                "my pet's name is "
            };

            for (const std::string_view cue : relationCues)
            {
                if (lowerText.find(cue) != std::string_view::npos)
                {
                    return true;
                }
            }

            return false;
        }


        [[nodiscard]]
        std::vector<std::string> extractCandidateSentences(
            const std::string_view userText,
            const std::size_t maximumCandidateBytes,
            const std::size_t maximumCandidates)
        {
            std::vector<std::string> extracted;
            extracted.reserve(maximumCandidates);

            std::unordered_set<std::string> normalizedSeen;
            normalizedSeen.reserve(maximumCandidates);

            std::size_t start{ 0 };

            const auto consider =
                [&](const std::string_view raw)
                {
                    if (extracted.size() >= maximumCandidates)
                    {
                        return;
                    }

                    const std::string_view sentence =
                        trimAsciiWhitespace(raw);

                    if (
                        sentence.size() < 8
                        || sentence.size() > maximumCandidateBytes
                        || sentence.starts_with('/')
                        || looksLikeExplicitMemoryCommand(sentence))
                    {
                        return;
                    }

                    const std::string lower =
                        asciiLower(sentence);

                    if (
                        !containsStableCue(lower)
                        && !normalizeMemoryFact(sentence).has_value())
                    {
                        return;
                    }

                    const std::string normalized =
                        candidateIdentity(sentence);

                    // Count observations by user turn, not by repeated text inside
                    // one submission. Saying the same sentence twice in one message
                    // must not immediately trigger durable promotion.
                    if (
                        normalized.empty()
                        || !normalizedSeen.insert(normalized).second)
                    {
                        return;
                    }

                    extracted.emplace_back(sentence);
                };

            for (std::size_t index = 0;
                 index < userText.size();
                 ++index)
            {
                const char character = userText[index];

                if (
                    character == '.'
                    || character == '!'
                    || character == '?'
                    || character == '\n')
                {
                    const std::size_t length =
                        index - start + (
                            character == '\n'
                                ? 0
                                : 1);

                    consider(
                        userText.substr(
                            start,
                            length));

                    start = index + 1;
                }
            }

            if (start < userText.size())
            {
                consider(
                    userText.substr(start));
            }

            return extracted;
        }


        [[nodiscard]]
        bool durableEquivalentExists(
            const MemoryRepository& repository,
            const std::string_view content)
        {
            return repository.hasActiveEquivalent(content);
        }
    }


    ConversationMemoryCandidateTracker::ConversationMemoryCandidateTracker(
        MemoryRepository& repository,
        ConversationMemoryCandidateConfig config)
        : repository_{ repository }
        , config_{ config }
    {
        if (config_.promotionObservationCount < 2)
        {
            throw std::invalid_argument{
                "Automatic conversation-memory promotion requires at least two observations."
            };
        }

        if (
            config_.maximumCandidates == 0
            || config_.maximumCandidateBytes == 0
            || config_.maximumCandidatesPerMessage == 0)
        {
            throw std::invalid_argument{
                "Conversation memory candidate bounds must be non-zero."
            };
        }
    }


    MemoryObservationResult
        ConversationMemoryCandidateTracker::observeUserMessage(
            const std::string_view userText)
    {
        MemoryObservationResult result;

        const std::vector<std::string> extracted =
            extractCandidateSentences(
                userText,
                config_.maximumCandidateBytes,
                config_.maximumCandidatesPerMessage);

        result.examinedSentences = extracted.size();

        const std::int64_t now =
            currentUnixMilliseconds();

        for (const std::string& content : extracted)
        {
            const std::string normalized =
                candidateIdentity(content);

            if (normalized.empty())
            {
                continue;
            }

            if (durableEquivalentExists(repository_, content))
            {
                ++result.candidatesDiscardedAsDurable;

                std::erase_if(
                    candidates_,
                    [&](const ConversationMemoryCandidate& candidate)
                    {
                        return
                            candidateIdentity(candidate.content)
                            == normalized;
                    });

                continue;
            }

            auto existing =
                std::find_if(
                    candidates_.begin(),
                    candidates_.end(),
                    [&](const ConversationMemoryCandidate& candidate)
                    {
                        return
                            candidateIdentity(candidate.content)
                            == normalized;
                    });

            if (existing == candidates_.end())
            {
                if (nextCandidateId_ == 0)
                {
                    throw std::runtime_error{
                        "Rose temporary memory candidate id space is exhausted."
                    };
                }

                if (candidates_.size() >= config_.maximumCandidates)
                {
                    // Bounded temporary memory: forget the oldest candidate rather
                    // than allowing ordinary conversation to grow memory forever.
                    candidates_.erase(candidates_.begin());
                }

                candidates_.push_back(
                    ConversationMemoryCandidate{
                        .id = nextCandidateId_,
                        .content = content,
                        .observationCount = 1,
                        .firstObservedUnixMilliseconds = now,
                        .lastObservedUnixMilliseconds = now
                    });

                ++result.candidatesAdded;

                if (nextCandidateId_ == std::numeric_limits<std::uint64_t>::max())
                {
                    nextCandidateId_ = 0;
                }
                else
                {
                    ++nextCandidateId_;
                }

                continue;
            }

            ++existing->observationCount;
            existing->lastObservedUnixMilliseconds = now;
            ++result.candidatesReinforced;

            if (
                existing->observationCount
                < config_.promotionObservationCount)
            {
                continue;
            }

            (void)repository_.remember(
                existing->content,
                MemoryKind::ConversationDerived,
                "conversation-derived:repeated-user-statement");

            candidates_.erase(existing);
            ++result.candidatesPromoted;
        }

        return result;
    }


    void ConversationMemoryCandidateTracker::clearTemporaryMemory()
    {
        candidates_.clear();
    }


    const std::vector<ConversationMemoryCandidate>&
        ConversationMemoryCandidateTracker::candidates() const noexcept
    {
        return candidates_;
    }


    std::optional<RememberMemoryResult>
        ConversationMemoryCandidateTracker::promoteCandidate(
            const std::uint64_t candidateId,
            const std::string_view source)
    {
        const auto found =
            std::find_if(
                candidates_.begin(),
                candidates_.end(),
                [candidateId](const ConversationMemoryCandidate& candidate)
                {
                    return candidate.id == candidateId;
                });

        if (found == candidates_.end())
        {
            return std::nullopt;
        }

        const RememberMemoryResult result =
            repository_.remember(
                found->content,
                MemoryKind::ConversationDerived,
                source);

        candidates_.erase(found);

        return result;
    }

} // namespace rose::memory
