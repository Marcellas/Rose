#pragma once

#include "model/ModelTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rose::conversation
{

    // ConversationConfig
    // -------------------------------------------------------------------------
    // Controls how much recent conversation is supplied to the language model
    // as immediate working context.
    //
    // IMPORTANT:
    // This is NOT Rose's long-term memory limit.
    //
    // Eventually Rose will distinguish:
    //
    //     Full conversation history
    //     Persistent memory
    //     Retrieved relevant memories
    //     Immediate model working context
    //
    // maxWorkingMessages only controls the last category.
    struct ConversationConfig
    {
        // Number of PREVIOUS messages retained in the immediate model context.
        //
        // Since a normal turn contains:
        //
        //     User
        //     Assistant
        //
        // a value of 12 represents roughly six completed turns.
        //
        // The current user's new message is added separately by RoseCore.
        std::size_t maxWorkingMessages{ 12 };
    };


    // Conversation
    // -------------------------------------------------------------------------
    // Owns the conversational state for Rose's current interactive session.
    //
    // RESPONSIBILITY:
    //
    //     - Store completed user/assistant turns
    //     - Produce a bounded recent working context
    //     - Clear the current conversation
    //
    // DOES NOT:
    //
    //     - Perform inference
    //     - Store long-term memories
    //     - Write anything to disk yet
    //     - Decide Rose's personality
    //     - Know anything about llama.cpp
    //
    // Keeping these responsibilities separate prevents conversation history
    // from becoming tangled with model-provider implementation details.
    class Conversation final
    {
    public:
        explicit Conversation(
            ConversationConfig config = {});


        // Commit one completed conversational turn.
        //
        // Both strings are accepted BY VALUE intentionally.
        //
        // This gives the caller two efficient choices:
        //
        //     commitTurn(existingString, existingString);  -> copies
        //     commitTurn(std::move(a), std::move(b));      -> moves
        //
        // Once committed, Conversation owns both pieces of text.
        void commitTurn(
            std::string userText,
            std::string assistantText);


        // Build the recent message sequence that should be supplied to the
        // model for the next request.
        //
        // The returned vector owns its own strings. That costs a small amount
        // of copying, but gives the request a very clear and safe lifetime.
        //
        // Model inference is orders of magnitude more expensive than these
        // small text copies, so correctness is more valuable here than trying
        // to optimize a few hundred bytes prematurely.
        [[nodiscard]]
        std::vector<model::ModelMessage>
            buildWorkingContext() const;


        // Remove the current conversational state.
        //
        // This does NOT affect future persistent/long-term memory systems.
        void clear() noexcept;


        [[nodiscard]]
        std::size_t storedMessageCount() const noexcept;


    private:
        // Find where the bounded working context should begin.
        //
        // We try to begin with a User message rather than supplying a dangling
        // Assistant response whose question has already been discarded.
        [[nodiscard]]
        std::size_t workingStartIndex() const noexcept;


        ConversationConfig config_;

        // Complete in-memory history for this running session.
        //
        // This is intentionally separate from the bounded context returned by
        // buildWorkingContext().
        //
        // Later this complete history can also be sent to Persistence while
        // only a relevant subset is supplied to the active model.
        std::vector<model::ModelMessage> messages_;
    };

} // namespace rose::conversation