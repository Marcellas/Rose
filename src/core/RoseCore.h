#pragma once

#include "conversation/Conversation.h"
#include "core/RoseActivity.h"
#include "logging/Logger.h"
#include "model/ModelTypes.h"

#include <cstddef>
#include <memory>
#include <string_view>

namespace rose::model
{
    class IModelProvider;
}

namespace rose::persistence
{
    class IConversationStore;
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


        void clearConversation();

        [[nodiscard]]
        std::size_t conversationMessageCount() const noexcept;

    private:
        std::unique_ptr<model::IModelProvider> modelProvider_;

        // Borrowed application-lifetime services.
        logging::Logger& logger_;

        conversation::Conversation conversation_;

        persistence::IConversationStore& conversationStore_;
    };

} // namespace rose::core
