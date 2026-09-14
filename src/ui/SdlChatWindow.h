#pragma once

#include "input/UserSubmission.h"
#include "platform/SdlRuntime.h"
#include "platform/TtfRuntime.h"
#include "ui/ChatBridge.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
struct TTF_Font;
struct TTF_TextEngine;
struct TTF_Text;

namespace rose::ui
{

    class RichTranscript;


    // Main-thread-owned graphical conversation frontend.
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

        [[nodiscard]]
        bool handleEvent(
            const SDL_Event& event);

        void update();
        void render();

    private:
        struct FontDeleter
        {
            void operator()(TTF_Font* font) const noexcept;
        };

        struct TextEngineDeleter
        {
            void operator()(TTF_TextEngine* engine) const noexcept;
        };

        struct TextDeleter
        {
            void operator()(TTF_Text* text) const noexcept;
        };

        struct WindowDeleter
        {
            void operator()(SDL_Window* window) const noexcept;
        };

        struct RendererDeleter
        {
            void operator()(SDL_Renderer* renderer) const noexcept;
        };

        using FontPtr =
            std::unique_ptr<TTF_Font, FontDeleter>;

        using TextEnginePtr =
            std::unique_ptr<TTF_TextEngine, TextEngineDeleter>;

        using TextPtr =
            std::unique_ptr<TTF_Text, TextDeleter>;

        using WindowPtr =
            std::unique_ptr<SDL_Window, WindowDeleter>;

        using RendererPtr =
            std::unique_ptr<SDL_Renderer, RendererDeleter>;


        void submitInput();

        void handleChatEvent(
            ChatEvent event);


        // ---------------------------------------------------------------------
        // Clipboard
        // ---------------------------------------------------------------------
        // SDL clipboard APIs are main-thread-only, which makes SdlChatWindow the
        // correct owner for these operations.
        void pasteClipboardIntoComposer();
        void copyComposerToClipboard() const;
        void cutComposerToClipboard();
        void copyTranscriptToClipboard() const;


        // ---------------------------------------------------------------------
        // Attachments
        // ---------------------------------------------------------------------
        void addDroppedFile(
            std::string_view pathText);

        void removeLastPendingAttachment();

        [[nodiscard]]
        std::string attachmentSummaryText() const;

        [[nodiscard]]
        std::string userTranscriptText(
            const input::UserSubmission& submission) const;


        // ---------------------------------------------------------------------
        // Composer editing
        // ---------------------------------------------------------------------
        void insertInputText(
            std::string_view text);

        void erasePreviousUtf8CodePoint();
        void eraseNextUtf8CodePoint();
        void moveInputCursorLeft();
        void moveInputCursorRight();
        void moveInputCursorVertical(int direction);

        [[nodiscard]]
        std::size_t previousUtf8Boundary(
            std::size_t offset) const noexcept;

        [[nodiscard]]
        std::size_t nextUtf8Boundary(
            std::size_t offset) const noexcept;

        void resetPreferredCaretX() noexcept;


        void refreshInputText();
        void refreshAttachmentText();


        platform::SdlRuntime& runtime_;
        platform::TtfRuntime& ttfRuntime_;
        ChatBridge& chatBridge_;

        WindowPtr window_;
        RendererPtr renderer_;

        FontPtr font_;
        TextEnginePtr textEngine_;
        TextPtr inputTextObject_;
        TextPtr attachmentTextObject_;

        // Rich transcript borrows renderer_ and textEngine_ and is destroyed first.
        std::unique_ptr<RichTranscript> transcript_;

        std::string inputText_;
        std::size_t inputCursorByteOffset_{ 0 };
        int inputWrapWidth_{ 0 };
        float inputScrollOffsetY_{ 0.0f };
        float preferredCaretX_{ -1.0f };
        bool inputTextDirty_{ true };

        std::vector<input::FileAttachment> pendingAttachments_;
        int attachmentWrapWidth_{ 0 };
        bool attachmentTextDirty_{ true };
    };

} // namespace rose::ui
