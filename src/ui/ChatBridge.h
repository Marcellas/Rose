#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>


namespace rose::ui
{

    // -----------------------------------------------------------------------------
    // ChatEventType
    // -----------------------------------------------------------------------------
    //
    // Messages sent FROM Rose's worker TO the graphical frontend.
    enum class ChatEventType
    {
        AssistantStarted,
        AssistantText,
        AssistantFinished,
        ConversationCleared,
        Error
    };


    // -----------------------------------------------------------------------------
    // ChatEvent
    // -----------------------------------------------------------------------------
    struct ChatEvent
    {
        ChatEventType type{
            ChatEventType::AssistantText
        };

        std::string text;
    };


    // -----------------------------------------------------------------------------
    // ChatBridge
    // -----------------------------------------------------------------------------
    //
    // Thread boundary between Rose's graphical UI and the RoseCore worker.
    //
    // DATA FLOW:
    //
    //     UI thread
    //         |
    //         | submitUserMessage()
    //         v
    //     request queue
    //         |
    //         v
    //     Rose worker
    //
    // and:
    //
    //     Rose worker
    //         |
    //         | postEvent()
    //         v
    //     event queue
    //         |
    //         v
    //     UI thread
    //
    // Neither queue exposes references to its internal storage.
    //
    // Strings are moved across the boundary so ownership is explicit.
    class ChatBridge final
    {
    public:
        // -------------------------------------------------------------------------
        // UI -> worker
        // -------------------------------------------------------------------------

        void submitUserMessage(
            std::string message);


        // Blocks the worker until:
        //
        //     - a message is available, or
        //     - shutdown has been requested.
        //
        // nullopt means shutdown.
        [[nodiscard]]
        std::optional<std::string> waitForUserMessage();


        // -------------------------------------------------------------------------
        // worker -> UI
        // -------------------------------------------------------------------------

        void postEvent(
            ChatEvent event);


        // Non-blocking because the SDL main loop must never wait on the model.
        [[nodiscard]]
        std::optional<ChatEvent> tryPopEvent();


        // -------------------------------------------------------------------------
        // Lifetime / shutdown
        // -------------------------------------------------------------------------

        void requestShutdown();


        [[nodiscard]]
        bool shutdownRequested() const;


    private:
        mutable std::mutex mutex_;

        std::condition_variable requestAvailable_;

        std::deque<std::string> userMessages_;

        std::deque<ChatEvent> events_;

        bool shutdownRequested_{ false };
    };

} // namespace rose::ui