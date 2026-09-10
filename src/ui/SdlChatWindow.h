#pragma once

#include "platform/SdlRuntime.h"
#include "ui/ChatBridge.h"
#include "platform/TtfRuntime.h"

#include <memory>
#include <string>
#include <vector>
#include <filesystem>


struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
struct TTF_Font;
struct TTF_TextEngine;
struct TTF_Text;

namespace rose::ui
{

    // -----------------------------------------------------------------------------
    // SdlChatWindow
    // -----------------------------------------------------------------------------
    //
    // Main-thread-owned graphical conversation frontend.
    //
    // It owns:
    //
    //     SDL_Window
    //     SDL_Renderer
    //     current input text
    //     temporary visible transcript
    //
    // It does NOT own RoseCore or perform model inference.
    class SdlChatWindow final
    {
    public:
        SdlChatWindow(
            platform::SdlRuntime& runtime,
            platform::TtfRuntime& ttfRuntime,
            ChatBridge& chatBridge,
            std::filesystem::path fontPath,
            int width,
            int height);

        ~SdlChatWindow();


        SdlChatWindow(const SdlChatWindow&) = delete;
        SdlChatWindow& operator=(const SdlChatWindow&) = delete;

        SdlChatWindow(SdlChatWindow&&) = delete;
        SdlChatWindow& operator=(SdlChatWindow&&) = delete;


        // Consume one event supplied by main's central SDL event pump.
        //
        // Returns false when this chat window has been asked to close.
        [[nodiscard]]
        bool handleEvent(
            const SDL_Event& event);


        // Pull pending worker events into UI-owned presentation state.
        void update();


        void render();


    private:
        struct FontDeleter
        {
            void operator()(
                TTF_Font* font) const noexcept;
        };

        struct TextEngineDeleter
        {
            void operator()(
                TTF_TextEngine* engine) const noexcept;
        };

        struct TextDeleter
        {
            void operator()(
                TTF_Text* text) const noexcept;
        };

        struct WindowDeleter
        {
            void operator()(
                SDL_Window* window) const noexcept;
        };


        struct RendererDeleter
        {
            void operator()(
                SDL_Renderer* renderer) const noexcept;
        };

        using FontPtr =
            std::unique_ptr<
            TTF_Font,
            FontDeleter>;


        using TextEnginePtr =
            std::unique_ptr<
            TTF_TextEngine,
            TextEngineDeleter>;

        using TextPtr =
            std::unique_ptr<
            TTF_Text,
            TextDeleter>;

        using WindowPtr =
            std::unique_ptr<
            SDL_Window,
            WindowDeleter>;

        using RendererPtr =
            std::unique_ptr<
            SDL_Renderer,
            RendererDeleter>;

        [[nodiscard]]
        std::string buildTranscriptText() const;

        void refreshTranscriptText();

        void submitInput();

        void handleChatEvent(
            ChatEvent event);


        // Remove the final UTF-8 code point from the input buffer.
        void eraseLastUtf8CodePoint();


        platform::SdlRuntime& runtime_;

        platform::TtfRuntime& ttfRuntime_;

        ChatBridge& chatBridge_;

        WindowPtr window_;

        RendererPtr renderer_;

        // -----------------------------------------------------------------------------
        // SDL_ttf resources
        // -----------------------------------------------------------------------------
        //
        // Declaration order is deliberate.
        //
        // Destruction happens in reverse:
        //
        //     TTF_Text
        //     TTF_TextEngine
        //     TTF_Font
        //     SDL_Renderer
        //     SDL_Window
        //
        // The text must disappear before the font/text engine, and the renderer-backed
        // text engine must disappear before the SDL renderer.

        FontPtr font_;

        TextEnginePtr textEngine_;

        TextPtr transcriptText_;

        TextPtr inputTextObject_;

        std::string inputText_;

        std::vector<std::string> transcript_;

        std::string streamingAssistantText_;

        // The canonical transcript data above changes independently from SDL_ttf.
        //
        // Rather than rebuilding text for every model token, ChatEvents mark the text
        // dirty. update() rebuilds it at most once per UI frame.
        bool transcriptDirty_{ true };


        // Avoid recalculating wrapped layout every frame when the window width has not
        // changed.
        int transcriptWrapWidth_{ 0 };

        // -----------------------------------------------------------------------------
        // Transcript scrolling
        // -----------------------------------------------------------------------------
        //
        // Scroll position is measured in pixels from the TOP of the laid-out
        // transcript.
        //
        //     0                     = oldest content / top
        //     transcriptMax...     = newest content / bottom
        //
        // followLatest_ gives chat-style behavior:
        //
        //     user at bottom -> new streamed text follows automatically
        //     user scrolls up -> new text does not yank them back down

        float transcriptScrollOffset_{ 0.0f };

        float transcriptMaxScrollOffset_{ 0.0f };

        bool followLatest_{ true };

        bool inputTextDirty_{ true };

        void refreshInputText();
    };

} // namespace rose::ui