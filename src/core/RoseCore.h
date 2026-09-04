#pragma once

#include "conversation/Conversation.h"
#include "model/ModelTypes.h"
#include "logging/Logger.h"
#include "core/RoseActivity.h"

#include <memory>
#include <string_view>

namespace rose::model
{
    class IModelProvider;
}

namespace rose::core
{

    // RoseCore
    // -------------------------------------------------------------------------
    // Coordinates Rose's major runtime systems.
    //
    // CURRENT OWNERSHIP:
    //
    //     RoseCore
    //       |
    //       +-- ModelProvider
    //       |
    //       +-- Conversation
    //
    // Later RoseCore will coordinate additional independent modules such as:
    //
    //     Agent
    //     Memory
    //     Tools
    //     Permissions
    //
    // RoseCore should remain a coordinator rather than becoming the
    // implementation of every subsystem.
    class RoseCore
    {
    public:
        explicit RoseCore(
            std::unique_ptr<model::IModelProvider> modelProvider,
            logging::Logger& logger,
            conversation::ConversationConfig conversationConfig = {});


        [[nodiscard]]
        model::ModelResponse processMessage(
            std::string_view message,
            const model::ModelTextCallback& onText = {},
            const RoseActivityCallback& onActivity = {});


        // Clear only the current working conversation.
        //
        // Persistent memories will eventually be a separate subsystem and
        // should NOT automatically disappear when this is called.
        void clearConversation() noexcept;


        [[nodiscard]]
        std::size_t conversationMessageCount() const noexcept;


    private:
        // RoseCore exclusively owns the active model provider.
        std::unique_ptr<model::IModelProvider> modelProvider_;

        // Borrowed application-wide logger.
        // main() owns it and guarantees it outlives RoseCore.
        logging::Logger& logger_;

        // RoseCore also owns the current interactive conversation.
        //
        // Conversation does not know which model provider is active.
        conversation::Conversation conversation_;

    };

} // namespace rose::core