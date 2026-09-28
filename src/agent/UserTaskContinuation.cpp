#include "agent/UserTaskContinuation.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace rose::agent
{
    namespace
    {
        [[nodiscard]]
        std::string lowerCopy(
            const std::string_view text)
        {
            std::string result{ text };
            std::transform(
                result.begin(),
                result.end(),
                result.begin(),
                [](const unsigned char value)
                {
                    return static_cast<char>(
                        std::tolower(value));
                });
            return result;
        }


        [[nodiscard]]
        bool containsAny(
            const std::string_view text,
            const std::initializer_list<std::string_view> needles) noexcept
        {
            for (const std::string_view needle : needles)
            {
                if (text.find(needle) != std::string_view::npos)
                {
                    return true;
                }
            }
            return false;
        }


        [[nodiscard]]
        bool explicitlyCancelsContinuation(
            const std::string_view currentUserText)
        {
            const std::string lower = lowerCopy(currentUserText);

            return containsAny(
                lower,
                {
                    "never mind",
                    "nevermind",
                    "forget that",
                    "cancel that",
                    "cancel the request",
                    "stop that",
                    "don't do that",
                    "do not do that"
                });
        }
    } // namespace


    bool shouldReuseUserTaskContinuation(
        const UserTaskContinuationState& state,
        const std::string_view currentUserText,
        const AgentRunProvenance& provenance)
    {
        if (
            state.userText.empty()
            || state.turnCount == 0
            || state.turnCount >= maximumUserTaskContinuationTurns
            || currentUserText.empty()
            || state.provenance.projectId != provenance.projectId
            || state.provenance.discussionId != provenance.discussionId
            || explicitlyCancelsContinuation(currentUserText))
        {
            return false;
        }

        // The routing model still decides semantic relatedness, and the current
        // message takes precedence over this prior user context. Reuse here merely
        // keeps unresolved user authority available for a few nearby turns.
        return true;
    }


    std::string appendUserTaskContinuationText(
        const std::string_view priorUserText,
        const std::string_view currentUserText)
    {
        if (priorUserText.empty())
        {
            return std::string{ currentUserText };
        }

        if (currentUserText.empty())
        {
            return std::string{ priorUserText };
        }

        static constexpr std::string_view separator{
            "\n\n--- next user clarification ---\n"
        };

        std::string combined;
        combined.reserve(
            priorUserText.size()
            + separator.size()
            + currentUserText.size());

        combined.append(priorUserText);
        combined.append(separator);
        combined.append(currentUserText);

        if (combined.size() <= maximumUserTaskContinuationBytes)
        {
            return combined;
        }

        // Preserve both the beginning of the original request (where paths and
        // requested actions commonly appear) and the newest clarification.
        static constexpr std::string_view truncationMarker{
            "\n\n--- older user clarification text truncated ---\n\n"
        };

        const std::size_t markerBytes = truncationMarker.size();
        const std::size_t available =
            maximumUserTaskContinuationBytes > markerBytes
                ? maximumUserTaskContinuationBytes - markerBytes
                : 0u;

        const std::size_t firstBytes = available / 2u;
        const std::size_t lastBytes = available - firstBytes;

        std::string bounded;
        bounded.reserve(maximumUserTaskContinuationBytes);
        bounded.append(combined.substr(0, firstBytes));
        bounded.append(truncationMarker);

        if (lastBytes < combined.size())
        {
            bounded.append(
                combined.substr(
                    combined.size() - lastBytes));
        }
        else
        {
            bounded.append(combined);
        }

        return bounded;
    }


    void rememberUserTaskContinuation(
        UserTaskContinuationState& state,
        std::string userText,
        AgentRunProvenance provenance)
    {
        if (userText.size() > maximumUserTaskContinuationBytes)
        {
            userText.resize(maximumUserTaskContinuationBytes);
        }

        state.userText = std::move(userText);
        state.provenance = std::move(provenance);

        if (state.turnCount < maximumUserTaskContinuationTurns)
        {
            ++state.turnCount;
        }
    }


    void clearUserTaskContinuation(
        UserTaskContinuationState& state) noexcept
    {
        state.userText.clear();
        state.provenance = {};
        state.turnCount = 0;
    }


    bool hasUserTaskContinuation(
        const UserTaskContinuationState& state) noexcept
    {
        return !state.userText.empty() && state.turnCount > 0;
    }

} // namespace rose::agent
