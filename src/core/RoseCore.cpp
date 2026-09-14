#include "core/RoseCore.h"

#include "model/IModelProvider.h"
#include "persistence/IConversationStore.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rose::core
{

    RoseCore::RoseCore(
        std::unique_ptr<model::IModelProvider> modelProvider,
        logging::Logger& logger,
        persistence::IConversationStore& conversationStore,
        conversation::ConversationConfig config)
        : modelProvider_{
            std::move(modelProvider)
        }
        , logger_{ logger }
        , conversation_{ config }
        , conversationStore_{ conversationStore }
    {
        if (!modelProvider_)
        {
            throw std::invalid_argument{
                "RoseCore requires a valid ModelProvider."
            };
        }


        auto storedTurns =
            conversationStore_.loadTurns();

        for (auto& turn : storedTurns)
        {
            conversation_.commitTurn(
                std::move(turn.userText),
                std::move(turn.assistantText));
        }
    }


    model::ModelResponse RoseCore::processMessage(
        const std::string_view message,
        const model::ModelTextCallback& onText,
        const RoseActivityCallback& onActivity)
    {
        return processMessage(
            message,
            std::string_view{},
            onText,
            onActivity);
    }


    model::ModelResponse RoseCore::processMessage(
        const std::string_view message,
        const std::string_view transientContext,
        const model::ModelTextCallback& onText,
        const RoseActivityCallback& onActivity)
    {
        if (message.empty())
        {
            throw std::invalid_argument{
                "RoseCore cannot process an empty user message."
            };
        }


        // Canonical text is what the user actually asked. It is the only user text
        // that will be persisted after a successful turn.
        std::string userText{
            message
        };


        std::vector<model::ModelMessage> requestMessages =
            conversation_.buildWorkingContext();


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


        // Build a request-local version of the current message. Attached source
        // material belongs here, not in the persisted canonical conversation turn.
        std::string modelUserText =
            userText;

        if (!transientContext.empty())
        {
            modelUserText +=
                "\n\n<rose_transient_context>\n";

            modelUserText.append(
                transientContext.data(),
                transientContext.size());

            modelUserText +=
                "\n</rose_transient_context>";
        }


        requestMessages.push_back(
            model::ModelMessage{
                .role = model::ModelRole::User,
                .content = std::move(modelUserText)
            });


        model::ModelRequest request{
            .messages = std::move(requestMessages),
            .sampling = {}
        };


        // Exact provider-side token accounting decides the final working context.
        // We remove only complete historical user/assistant pairs; the system prompt
        // and current user submission (including its transient attachments) survive.
        while (true)
        {
            const model::ModelContextUsage usage =
                modelProvider_->inspectContext(
                    request);

            logger_.debug(
                "RoseCore",
                "Context usage: "
                + std::to_string(usage.promptTokens)
                + " prompt + "
                + std::to_string(usage.requestedGenerationTokens)
                + " response = "
                + std::to_string(usage.totalRequestedTokens())
                + " / "
                + std::to_string(usage.contextCapacity));

            if (usage.fits())
            {
                break;
            }

            if (request.messages.size() >= 4)
            {
                logger_.debug(
                    "RoseCore",
                    "Context exceeds capacity; removing oldest "
                    "user/assistant turn from working context.");

                request.messages.erase(
                    request.messages.begin() + 1,
                    request.messages.begin() + 3);

                continue;
            }

            throw std::runtime_error{
                "This message and its attached context are too large to process in one request."
            };
        }


        if (onActivity)
        {
            onActivity(
                RoseActivity::Thinking);
        }


        bool speakingStarted{ false };

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


            // Persistence contains the user's canonical request, not the contents of
            // every file Rose happened to read for this turn.
            conversationStore_.appendTurn(
                userText,
                response.text);

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
            if (onActivity)
            {
                onActivity(
                    RoseActivity::Confused);
            }

            throw;
        }


        return response;
    }


    void RoseCore::clearConversation()
    {
        conversationStore_.clear();
        conversation_.clear();
    }


    std::size_t RoseCore::conversationMessageCount() const noexcept
    {
        return conversation_.storedMessageCount();
    }

} // namespace rose::core
