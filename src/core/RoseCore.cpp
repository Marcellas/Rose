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
        const std::string_view message,
        const model::ModelTextCallback& onText,
        const RoseActivityCallback& onActivity)
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
        };


        // -------------------------------------------------------------------------
// Generate the assistant response.
// -------------------------------------------------------------------------
//
// Activity flow:
//
//     Thinking
//        |
//        | first visible streamed text
//        v
//     Speaking
//        |
//        | response successfully completes
//        v
//       Idle
//
// The provider still owns inference. RoseCore only translates model activity
// into provider-independent Rose activity.
//
// IMPORTANT:
// We use streaming when either:
//
//     - the frontend wants streamed text, OR
//     - an activity observer wants to know when Rose begins speaking.
//
// The second case matters because detecting the first visible response chunk is
// what lets Rose transition accurately from Thinking to Speaking.
        if (onActivity)
        {
            onActivity(
                RoseActivity::Thinking);
        }


        bool speakingStarted{ false };


        // -------------------------------------------------------------------------
        // Forward visible model text.
        // -------------------------------------------------------------------------
        //
        // This callback wraps the caller's normal text callback.
        //
        // It has two jobs:
        //
        //     1. Detect the first visible piece of assistant output.
        //     2. Forward that piece unchanged to the actual frontend.
        //
        // The callback does not retain the string_view. Its lifetime remains limited to
        // the provider callback invocation.
        const model::ModelTextCallback streamedText =
            [&onText,
            &onActivity,
            &speakingStarted](
                const std::string_view text)
            {
                if (
                    !speakingStarted
                    && !text.empty())
                {
                    speakingStarted = true;

                    if (onActivity)
                    {
                        onActivity(
                            RoseActivity::Speaking);
                    }
                }


                if (onText)
                {
                    onText(text);
                }
            };


        model::ModelResponse response;


        try
        {
            // Streaming is required not only when the frontend wants text chunks, but
            // also when Rose needs to detect the first visible chunk for activity
            // transitions.
            if (onText || onActivity)
            {
                response =
                    modelProvider_->generateStreaming(
                        request,
                        streamedText);
            }
            else
            {
                response =
                    modelProvider_->generate(
                        request);
            }


            // Commit only after generation succeeds.
            //
            // Streaming output may already have been shown to the user, but incomplete
            // or failed responses must not contaminate Conversation history.
            conversation_.commitTurn(
                std::move(userText),
                response.text);


            if (onActivity)
            {
                onActivity(
                    RoseActivity::Idle);
            }
        }
        catch (...)
        {
            // For now Confused represents an operation that failed.
            //
            // Later we may separate recoverable confusion from actual Error state, but
            // keeping one failure state is sufficient for the MVP.
            if (onActivity)
            {
                onActivity(
                    RoseActivity::Confused);
            }

            throw;
        }


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