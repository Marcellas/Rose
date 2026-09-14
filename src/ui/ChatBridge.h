#pragma once

#include "artifacts/Artifact.h"
#include "input/UserSubmission.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace rose::ui
{

    // Messages sent FROM Rose's worker TO the graphical frontend.
    enum class ChatEventType
    {
        AssistantStarted,
        AssistantText,
        AssistantFinished,
        ArtifactReady,
        ConversationCleared,
        Error
    };


    struct ChatEvent
    {
        ChatEventType type{
            ChatEventType::AssistantText
        };

        std::string text;

        // Only populated for ArtifactReady. A value type is deliberate: the worker
        // transfers a durable path/metadata description, never an SDL resource.
        std::optional<artifacts::Artifact> artifact;
    };


    // -----------------------------------------------------------------------------
    // ChatBridge
    // -----------------------------------------------------------------------------
    //
    // Thread boundary between the SDL/UI thread and Rose's worker.
    //
    // UI -> worker moves one UserSubmission so text and attachments remain one
    // coherent request. Worker -> UI can now also move Artifact descriptors.
    class ChatBridge final
    {
    public:
        void submitUserSubmission(
            input::UserSubmission submission);

        [[nodiscard]]
        std::optional<input::UserSubmission>
            waitForUserSubmission();

        // Convenience path retained for simple text-only callers/tests.
        void submitUserMessage(
            std::string message);


        void postEvent(
            ChatEvent event);

        [[nodiscard]]
        std::optional<ChatEvent> tryPopEvent();


        void requestShutdown();

        [[nodiscard]]
        bool shutdownRequested() const;

    private:
        mutable std::mutex mutex_;

        std::condition_variable requestAvailable_;

        std::deque<input::UserSubmission> userSubmissions_;

        std::deque<ChatEvent> events_;

        bool shutdownRequested_{ false };
    };

} // namespace rose::ui
