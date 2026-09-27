#pragma once

#include "conversation/Conversation.h"
#include "core/RoseActivity.h"
#include "logging/Logger.h"
#include "model/ModelTypes.h"

#include <cstddef>
#include <memory>
#include <string_view>

namespace rose::memory
{
    class IMemoryObserver;
    class IMemoryRetriever;
}

namespace rose::model
{
    class IModelProvider;
}

namespace rose::persistence
{
    class IConversationStore;
}

namespace rose::policy
{
    class ContentPolicy;
}

namespace rose::core
{

    // Coordinates Rose's high-level application behavior while keeping the active
    // model provider replaceable and persistence independent from presentation.
    class RoseCore final
    {
    public:
        RoseCore(
            std::unique_ptr<model::IModelProvider> modelProvider,
            logging::Logger& logger,
            persistence::IConversationStore& conversationStore,
            const policy::ContentPolicy& contentPolicy,
            memory::IMemoryRetriever* memoryRetriever = nullptr,
            memory::IMemoryObserver* memoryObserver = nullptr,
            conversation::ConversationConfig config = {});


        // Standard text-only request path.
        [[nodiscard]]
        model::ModelResponse processMessage(
            std::string_view message,
            const model::ModelTextCallback& onText = {},
            const RoseActivityCallback& onActivity = {});


        // Request path with ephemeral source material such as explicitly attached
        // files. transientContext is supplied to the model for THIS generation only.
        // It is deliberately not written to Conversation/Persistence as if the user
        // typed the file contents into chat.
        [[nodiscard]]
        model::ModelResponse processMessage(
            std::string_view message,
            std::string_view transientContext,
            const model::ModelTextCallback& onText,
            const RoseActivityCallback& onActivity);


        void commitAuthoritativeTurn(
            std::string_view userText,
            std::string_view assistantText);


        void clearConversation();

        // Reload the in-memory working history from the currently selected
        // IConversationStore. This is the hand-off point used when the desktop
        // switches between persistent Discussion transcripts. The model provider
        // remains loaded and Rose's long-term memory repository is untouched.
        void reloadConversation();

        [[nodiscard]]
        std::size_t conversationMessageCount() const noexcept;

    private:
        std::unique_ptr<model::IModelProvider> modelProvider_;

        // Borrowed application-lifetime services.
        logging::Logger& logger_;

        conversation::Conversation conversation_;

        persistence::IConversationStore& conversationStore_;

        // Optional borrowed long-term-memory retrieval boundary. RoseCore does
        // not own or mutate long-term memory; it only asks for a bounded set of
        // relevant records for the current request.
        memory::IMemoryRetriever* memoryRetriever_{ nullptr };

        // Optional borrowed post-commit observer for bounded temporary-memory
        // consolidation. Durable persistence remains owned by memory modules.
        memory::IMemoryObserver* memoryObserver_{ nullptr };

        // Borrowed application-lifetime policy. RoseCore uses it only to build
        // provider-neutral behavior guidance; tool enforcement remains separate.
        const policy::ContentPolicy& contentPolicy_;
    };

} // namespace rose::core
