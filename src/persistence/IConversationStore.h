#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rose::persistence
{

    // -------------------------------------------------------------------------
    // StoredConversationTurn
    // -------------------------------------------------------------------------
    //
    // Persistence deliberately stores complete conversation turns rather than
    // model-provider objects.
    //
    // That keeps persistent Rose data independent from llama.cpp, Qwen, OpenAI,
    // or any future ModelProvider.
    struct StoredConversationTurn
    {
        std::string userText;
        std::string assistantText;

        // Unix timestamp in milliseconds.
        //
        // Keeping the serialized representation numeric avoids embedding any
        // locale or formatted-date assumptions in Rose's persistent data.
        std::int64_t unixTimeMilliseconds{ 0 };
    };


    // -------------------------------------------------------------------------
    // IConversationStore
    // -------------------------------------------------------------------------
    //
    // Persistent storage boundary for conversation history.
    //
    // RoseCore should depend on this interface, not a particular database or
    // file format.
    class IConversationStore
    {
    public:
        virtual ~IConversationStore() = default;


        // Restore all successfully persisted turns in chronological order.
        [[nodiscard]]
        virtual std::vector<StoredConversationTurn>
            loadTurns() = 0;


        // Persist one complete successful user/assistant turn.
        //
        // Implementations should not silently discard failures. If persistence
        // fails, they should throw so Rose can surface the problem.
        virtual void appendTurn(
            std::string_view userText,
            std::string_view assistantText) = 0;


        // Remove the stored conversation.
        //
        // This will eventually back Rose's explicit "new conversation" /
        // destructive-clear behavior.
        virtual void clear() = 0;
    };

} // namespace rose::persistence