#include "core/RoseCore.h"

#include "model/IModelProvider.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rose::core
{

    RoseCore::RoseCore(
        std::unique_ptr<model::IModelProvider> modelProvider,
        logging::Logger& logger,
        const conversation::ConversationConfig conversationConfig)
        : modelProvider_{ std::move(modelProvider) }
        , logger_{ logger }
        , conversation_{ conversationConfig }
    {
        if (!modelProvider_)
        {
            throw std::invalid_argument{
                "RoseCore requires a valid ModelProvider."
            };
        }
    }


    model::ModelResponse RoseCore::processMessage(
        const std::string_view message)
    {
        if (message.empty())
        {
            throw std::invalid_argument{
                "RoseCore cannot process an empty user message."
            };
        }


        // -------------------------------------------------------------------------
        // Own the current user's input.
        // -------------------------------------------------------------------------
        //
        // The incoming string_view only borrows the caller's text buffer.
        //
        // Conversation history needs its own copy because it must remain valid
        // after this function returns.
        std::string userText{ message };


        // -------------------------------------------------------------------------
        // Retrieve Rose's recent working conversation.
        // -------------------------------------------------------------------------

        std::vector<model::ModelMessage> requestMessages =
            conversation_.buildWorkingContext();


        // Temporary diagnostic.
        //
        // Once our logging subsystem exists, this information will become a
        // structured Verbose-level event instead of console output.
        logger_.debug(
            "RoseCore",
            "Stored history messages: "
            + std::to_string(
                conversation_.storedMessageCount()));

        logger_.debug(
            "RoseCore",
            "Working-context messages: "
            + std::to_string(
                requestMessages.size()));


        // -------------------------------------------------------------------------
        // Rose's system identity
        // -------------------------------------------------------------------------
        //
        // IMPORTANT:
        //
        // This belongs to RoseCore / eventually Agent configuration.
        //
        // It does NOT belong in LlamaCppModelProvider because the language model is
        // not Rose. If we replace Qwen with another model tomorrow, Rose should
        // retain the same identity.
        //
        // /no_think is currently a Qwen instruction. We are using it temporarily
        // while Qwen is Rose's active local model. Later reasoning preference will
        // become a provider-neutral ModelRequest capability.
        requestMessages.insert(
            requestMessages.begin(),
            model::ModelMessage{
                .role = model::ModelRole::System,
                .content =
                    "You are Rose, a local-first desktop AI assistant. "
                    "Use the previous conversation messages when they are relevant. "
                    "Answer the user directly and naturally. "
                    "/no_think"
            });


        // -------------------------------------------------------------------------
        // Add the CURRENT user message.
        // -------------------------------------------------------------------------
        //
        // It is not committed to Conversation yet. We only commit completed turns
        // after model generation succeeds.
        requestMessages.push_back(
            model::ModelMessage{
                .role = model::ModelRole::User,
                .content = userText
            });


        model::ModelRequest request{
            .messages = std::move(requestMessages),
            .sampling = {},
            .maxGeneratedTokens = 256
        };


        model::ModelResponse response =
            modelProvider_->generate(request);


        // Only after successful inference do we permanently add this completed
        // user/assistant pair to the current Conversation.
        conversation_.commitTurn(
            std::move(userText),
            response.text);


        logger_.debug(
            "RoseCore",
            "Stored messages after turn: "
            + std::to_string(
                conversation_.storedMessageCount()));


        return response;
    }


    void RoseCore::clearConversation() noexcept
    {
        conversation_.clear();
    }


    std::size_t
        RoseCore::conversationMessageCount() const noexcept
    {
        return conversation_.storedMessageCount();
    }

} // namespace rose::core