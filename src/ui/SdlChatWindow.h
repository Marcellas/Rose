#pragma once

#include "platform/SdlRuntime.h"
#include "input/UserSubmission.h"
#include "ui/ArtifactCardStack.h"
#include "ui/ChatBridge.h"
#include "ui/TextEditHistory.h"
#include "ui/InputRecallHistory.h"
#include "platform/TtfRuntime.h"
#include "ui/RichTranscript.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>


struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
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

        // Presence-first desktop shell controls. Closing the chat window now hides
        // it instead of shutting Rose down; the avatar context menu can reopen it.
        void show();
        void hide() noexcept;
        [[nodiscard]] bool visible() const noexcept;

        // Replace the composer text without submitting it. Menu actions can use
        // this to guide the user into an existing conversational workflow.
        void setDraftText(std::string text);

        // Explicit menu action captures and submits a timestamped image for
        // analysis; ordinary clipboard paste only stages a preview.
        void stageScreenCapture();


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

        struct TextureDeleter
        {
            void operator()(SDL_Texture* texture) const noexcept;
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

        using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;

        [[nodiscard]]
        std::string buildTranscriptText() const;

        void refreshTranscriptText();

        void setFormattedTranscript(bool formatted);

        void submitInput();

        void handleChatEvent(
            ChatEvent event);


        // -------------------------------------------------------------------------
        // Attachments
        // -------------------------------------------------------------------------
        //
        // Dropped files are staged in the composer until the user presses Enter.
        // Only paths cross the UI -> worker boundary; reading/parsing remains
        // worker-owned through AttachmentIngestion.
        void addDroppedFile(
            const char* utf8Path);

        [[nodiscard]]
        bool removeDroppedFile(
            const std::filesystem::path& path);

        void clearPendingAttachments();

        void refreshAttachmentSummaryText();
        void refreshStagedImagePreview();

        [[nodiscard]]
        std::string pendingAttachmentSummary() const;


        // -------------------------------------------------------------------------
        // Composer editing
        // -------------------------------------------------------------------------
        //
        // inputText_ remains the canonical UTF-8 byte string that will be sent to
        // Rose. Visual wrapping never inserts artificial newline characters into it.
        //
        // The cursor is stored as a UTF-8 BYTE OFFSET because SDL text events and
        // SDL_ttf substring APIs also use byte offsets. Every movement helper keeps
        // that offset on a code-point boundary.
        void insertInputText(
            std::string_view text,
            TextEditHistory::Kind editKind =
                TextEditHistory::Kind::Typing);

        void erasePreviousUtf8CodePoint();

        void eraseNextUtf8CodePoint();

        void moveInputCursorLeft(
            bool extendSelection);

        void moveInputCursorRight(
            bool extendSelection);

        void moveInputCursorVertical(
            int direction,
            bool extendSelection);

        void moveInputCursorTo(
            std::size_t byteOffset,
            bool extendSelection);

        [[nodiscard]]
        std::size_t previousUtf8Boundary(
            std::size_t offset) const noexcept;

        [[nodiscard]]
        std::size_t nextUtf8Boundary(
            std::size_t offset) const noexcept;

        enum class TextRunClass
        {
            Whitespace,
            Word,
            Punctuation
        };

        struct ByteRange
        {
            std::size_t start{ 0 };
            std::size_t end{ 0 };

            [[nodiscard]]
            bool empty() const noexcept
            {
                return start >= end;
            }
        };

        [[nodiscard]]
        static std::size_t previousUtf8BoundaryIn(
            std::string_view text,
            std::size_t offset) noexcept;

        [[nodiscard]]
        static std::size_t nextUtf8BoundaryIn(
            std::string_view text,
            std::size_t offset) noexcept;

        [[nodiscard]]
        static TextRunClass classifyTextRun(
            std::string_view text,
            std::size_t offset) noexcept;

        [[nodiscard]]
        static ByteRange wordRangeAt(
            std::string_view text,
            std::size_t offset) noexcept;

        [[nodiscard]]
        static std::size_t previousWordBoundary(
            std::string_view text,
            std::size_t offset) noexcept;

        [[nodiscard]]
        static std::size_t nextWordBoundary(
            std::string_view text,
            std::size_t offset) noexcept;

        void moveInputCursorWordLeft(
            bool extendSelection);

        void moveInputCursorWordRight(
            bool extendSelection);

        void resetPreferredCaretX() noexcept;

        [[nodiscard]]
        TextEditHistory::SelectionState
        currentInputSelectionState() const noexcept;

        void restoreInputSelectionState(
            const TextEditHistory::SelectionState& state) noexcept;

        void undoInputEdit();

        void redoInputEdit();

        void recallPreviousInput();

        void recallNextInput();

        void applyRecalledInput(
            std::string_view text);


        // -------------------------------------------------------------------------
        // Selection + clipboard
        // -------------------------------------------------------------------------
        [[nodiscard]]
        bool hasInputSelection() const noexcept;

        [[nodiscard]]
        std::size_t inputSelectionStart() const noexcept;

        [[nodiscard]]
        std::size_t inputSelectionEnd() const noexcept;

        void clearInputSelection() noexcept;

        bool deleteInputSelection();

        [[nodiscard]]
        bool hasTranscriptSelection() const noexcept;

        [[nodiscard]]
        std::size_t transcriptSelectionStart() const noexcept;

        [[nodiscard]]
        std::size_t transcriptSelectionEnd() const noexcept;

        void clearTranscriptSelection() noexcept;

        void selectAllFocusedText();

        void copyFocusedSelectionToClipboard();

        void cutInputSelectionToClipboard();

        void pasteClipboardText();
        void stageTemporaryImage(const std::filesystem::path& path);


        // -------------------------------------------------------------------------
        // Context menu
        // -------------------------------------------------------------------------
        //
        // The menu is owned and rendered by SdlChatWindow. It is not a native
        // Win32 popup, so the editing surface remains portable and all UI state
        // stays on the SDL/main thread.
        struct ContextMenuLayout
        {
            bool valid{ false };
            float x{ 0.0f };
            float y{ 0.0f };
            float width{ 0.0f };
            float itemHeight{ 0.0f };
        };

        void openContextMenu(
            float x,
            float y);

        void closeContextMenu() noexcept;

        void updateContextMenuHover(
            float x,
            float y) noexcept;

        [[nodiscard]]
        int contextMenuItemIndexForPoint(
            float x,
            float y) const noexcept;

        void activateContextMenuItem(
            int itemIndex);

        [[nodiscard]]
        bool contextMenuUndoEnabled() const noexcept;

        [[nodiscard]]
        bool contextMenuRedoEnabled() const noexcept;

        [[nodiscard]]
        bool contextMenuCopyEnabled() const noexcept;

        [[nodiscard]]
        bool contextMenuCutEnabled() const noexcept;

        [[nodiscard]]
        bool contextMenuPasteEnabled() const noexcept;

        [[nodiscard]]
        bool contextMenuSelectAllEnabled() const noexcept;

        [[nodiscard]]
        bool contextMenuCopyMessageEnabled() const noexcept;

        [[nodiscard]]
        std::optional<ByteRange> transcriptMessageRangeAt(
            std::size_t byteOffset) const noexcept;

        void copyContextMessageToClipboard();

        void renderContextMenu(
            int windowWidth,
            int windowHeight);


        // -------------------------------------------------------------------------
        // Mouse text selection
        // -------------------------------------------------------------------------
        enum class TextFocus
        {
            Composer,
            Transcript
        };

        enum class SelectionDrag
        {
            None,
            Composer,
            Transcript
        };

        enum class ScrollDrag
        {
            None,
            Composer,
            Transcript,
            FormattedTranscript
        };

        struct ScrollbarSnapshot
        {
            float x{ 0.0f };
            float y{ 0.0f };
            float width{ 0.0f };
            float height{ 0.0f };
            float thumbHeight{ 0.0f };
            float maximumOffset{ 0.0f };
        };

        void dragScrollbar(float mouseY);

        enum class PageScrollTarget
        {
            None,
            Top,
            Bottom
        };

        static constexpr std::size_t transcriptPageSize{ 8 };
        static constexpr std::size_t transcriptPageOverlap{ 4 };
        void appendTranscriptEntry(std::string display, std::string original = {});
        void showLatestTranscriptPage();
        void showOlderTranscriptPage();
        void showNewerTranscriptPage();
        void prepareTranscriptPage();
        void rebuildRichTranscriptPage();

        struct TextLayoutSnapshot
        {
            bool valid{ false };

            float inputAreaX{ 0.0f };
            float inputAreaY{ 0.0f };
            float inputAreaWidth{ 0.0f };
            float inputAreaHeight{ 0.0f };
            float inputTextX{ 0.0f };
            float inputTextY{ 0.0f };

            float transcriptAreaX{ 0.0f };
            float transcriptAreaY{ 0.0f };
            float transcriptAreaWidth{ 0.0f };
            float transcriptAreaHeight{ 0.0f };
            float transcriptTextX{ 0.0f };
            float transcriptTextY{ 0.0f };
        };

        [[nodiscard]]
        bool pointInside(
            float x,
            float y,
            float left,
            float top,
            float width,
            float height) const noexcept;

        [[nodiscard]]
        std::size_t inputOffsetForPoint(
            float x,
            float y);

        [[nodiscard]]
        std::size_t transcriptOffsetForPoint(
            float x,
            float y);

        void beginMouseSelection(
            float x,
            float y);

        void selectWordAtPoint(
            float x,
            float y);

        void updateMouseSelection(
            float x,
            float y);

        void endMouseSelection() noexcept;

        void drawSelectionRange(
            TTF_Text* text,
            std::size_t start,
            std::size_t end,
            float originX,
            float originY);


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

        // The Windows symbol fallback outlives the primary font because SDL_ttf
        // borrows it for glyphs such as forall/exists missing from Segoe UI.
        FontPtr mathFallbackFont_;
        FontPtr font_;

        TextEnginePtr textEngine_;

        TextPtr transcriptText_;

        TextPtr inputTextObject_;

        // Small persistent labels for the in-window editing context menu.
        // They borrow the same renderer-backed text engine and font as the rest
        // of the chat UI and are destroyed before those shared resources.
        TextPtr contextMenuUndoText_;
        TextPtr contextMenuRedoText_;
        TextPtr contextMenuCutText_;
        TextPtr contextMenuCopyText_;
        TextPtr contextMenuPasteText_;
        TextPtr contextMenuSelectAllText_;
        TextPtr contextMenuCopyMessageText_;

        // One persistent label summarizes files staged by drag/drop. It borrows
        // font_ and textEngine_ and is destroyed before them.
        TextPtr attachmentSummaryText_;

        TextPtr transcriptToggleText_;

        // Rich messages share the UI renderer and text engine. The selectable
        // text view remains available through the transcript header switch.
        std::unique_ptr<RichTranscript> richTranscript_;
        // Precise selection is available immediately; formatting is optional.
        bool formattedTranscript_{ false };
        float transcriptToggleX_{ 0.0f };
        float transcriptToggleY_{ 0.0f };
        float transcriptToggleWidth_{ 0.0f };
        float transcriptToggleHeight_{ 0.0f };

        // Artifact cards own only UI presentation resources (TTF text + image
        // textures). Artifact files themselves remain owned by ArtifactStore.
        std::unique_ptr<ArtifactCardStack> artifactCards_;

        // Canonical composer contents. Visual wrapping is presentation only.
        std::string inputText_;
        // SDL_ttf sees a short slice; these offsets map it to the full draft.
        std::size_t inputDisplayStartOffset_{ 0 };
        std::size_t inputDisplayEndOffset_{ 0 };
        std::string inputWindowTitle_{ "Rose" };

        // UTF-8 byte offset into inputText_. This is never allowed to point into
        // the middle of a multi-byte UTF-8 code point.
        std::size_t inputCursorByteOffset_{ 0 };

        // When present, this is the fixed end of the composer selection. The
        // moving end is inputCursorByteOffset_. Keeping both as byte offsets
        // matches SDL_ttf and avoids a second character-index representation.
        std::optional<std::size_t> inputSelectionAnchorByteOffset_;

        // Delta-based history belongs to the composer presentation layer. It is
        // bounded internally by both entry count and retained changed bytes.
        TextEditHistory inputEditHistory_;

        // Submitted-prompt recall is deliberately separate from Undo/Redo. It is
        // session-local and bounded internally by entry count and retained bytes.
        InputRecallHistory inputRecallHistory_;

        // Width currently applied to inputTextObject_ for word wrapping.
        int inputWrapWidth_{ 0 };
        int inputTextWidth_{ 0 };
        int inputTextHeight_{ 0 };
        bool inputLayoutDirty_{ true };
        int requestedLayoutWidth_{ 0 };
        int settledLayoutWidth_{ 0 };
        std::uint64_t layoutWidthChangedAt_{ 0 };

        // Vertical scroll inside the composer once it reaches its maximum height.
        float inputScrollOffsetY_{ 0.0f };
        float inputMaxScrollOffsetY_{ 0.0f };
        bool inputCaretScrollPending_{ true };

        // Horizontal pixel column retained while moving repeatedly with Up/Down.
        // A negative value means the next vertical movement should capture the
        // current caret column first.
        float preferredCaretX_{ -1.0f };

        // Files dropped onto the chat window but not yet submitted. This list is
        // session/UI state only and is moved into one UserSubmission on Enter.
        std::vector<input::FileAttachment> pendingAttachments_;
        std::vector<std::filesystem::path> temporaryImages_;
        TexturePtr stagedImagePreview_;
        float stagedImageWidth_{ 0.0f };
        float stagedImageHeight_{ 0.0f };

        std::vector<std::string> transcript_;
        // Original text is retained for formatted rendering. Only the current
        // page is ever handed to SDL_ttf or the rich renderer.
        std::vector<std::string> transcriptOriginal_;
        std::size_t transcriptPageStart_{ 0 };
        std::size_t transcriptPageEnd_{ 0 };
        PageScrollTarget pageScrollTarget_{ PageScrollTarget::None };
        bool richPageDirty_{ true };
        std::size_t richStreamingBytes_{ 0 };
        std::uint64_t lastRichStreamRefreshTicks_{ 0 };

        std::string streamingAssistantText_;

        // Exact UTF-8 currently installed in transcriptText_. Transcript selection
        // byte offsets are always interpreted against this string.
        std::string displayedTranscriptText_;

        std::optional<std::size_t> transcriptSelectionAnchorByteOffset_;
        std::size_t transcriptSelectionCaretByteOffset_{ 0 };

        TextFocus textFocus_{ TextFocus::Composer };
        SelectionDrag selectionDrag_{ SelectionDrag::None };
        ScrollDrag scrollDrag_{ ScrollDrag::None };
        ScrollbarSnapshot composerScrollbar_;
        ScrollbarSnapshot transcriptScrollbar_;
        TextLayoutSnapshot textLayout_;

        bool contextMenuOpen_{ false };
        float contextMenuRequestedX_{ 0.0f };
        float contextMenuRequestedY_{ 0.0f };
        int contextMenuHoveredItem_{ -1 };
        ContextMenuLayout contextMenuLayout_;
        std::optional<ByteRange> contextMenuMessageRange_;
        std::optional<std::string> contextMenuFormattedMessage_;

        // The canonical transcript data above changes independently from SDL_ttf.
        //
        // Rather than rebuilding text for every model token, ChatEvents mark the text
        // dirty. update() rebuilds it at most once per UI frame.
        bool transcriptDirty_{ true };


        // Avoid recalculating wrapped layout every frame when the window width has not
        // changed.
        int transcriptWrapWidth_{ 0 };
        int transcriptTextHeight_{ 0 };
        bool transcriptLayoutDirty_{ true };
        std::uint64_t lastTranscriptRefreshTicks_{ 0 };

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
