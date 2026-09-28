#pragma once

#include "agent/AgentJournal.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace rose::agent
{
    inline constexpr std::size_t maximumUserTaskContinuationBytes{ 16u * 1024u };
    inline constexpr std::size_t maximumUserTaskContinuationTurns{ 4u };


    // Ephemeral, user-authored context for one unresolved tool-backed task.
    //
    // This is deliberately separate from conversation memory and assistant text.
    // Only verbatim user messages enter this state, so a later clarification may
    // restore path/intent grounding without turning a model-generated proposal into
    // execution authority.
    struct UserTaskContinuationState
    {
        std::string userText;
        AgentRunProvenance provenance;
        std::size_t turnCount{ 0 };
    };


    // Decide whether a pending unresolved task may be supplied to the next Agent
    // routing pass. The state is discussion/project scoped, bounded by turn count,
    // and explicit cancellation wording clears it.
    [[nodiscard]]
    bool shouldReuseUserTaskContinuation(
        const UserTaskContinuationState& state,
        std::string_view currentUserText,
        const AgentRunProvenance& provenance);


    // Append one user-authored clarification while keeping the state bounded.
    [[nodiscard]]
    std::string appendUserTaskContinuationText(
        std::string_view priorUserText,
        std::string_view currentUserText);


    void rememberUserTaskContinuation(
        UserTaskContinuationState& state,
        std::string userText,
        AgentRunProvenance provenance);


    void clearUserTaskContinuation(
        UserTaskContinuationState& state) noexcept;


    [[nodiscard]]
    bool hasUserTaskContinuation(
        const UserTaskContinuationState& state) noexcept;

} // namespace rose::agent
