#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

struct SDL_Renderer;
struct TTF_TextEngine;

namespace rose::ui
{

    // -----------------------------------------------------------------------------
    // RichTranscript
    // -----------------------------------------------------------------------------
    //
    // Owns the UI-only conversation presentation state. Model/conversation history
    // remains elsewhere and is never rewritten to satisfy renderer requirements.
    //
    // Completed assistant responses are parsed and cached as rich documents.
    // The response currently being generated is intentionally kept literal/plain so
    // partial Markdown or LaTeX tokens cannot confuse the parser mid-stream.
    class RichTranscript final
    {
    public:
        RichTranscript(
            SDL_Renderer& renderer,
            TTF_TextEngine& textEngine,
            std::filesystem::path fontPath,
            std::filesystem::path microTexResourceRoot);

        ~RichTranscript();

        RichTranscript(const RichTranscript&) = delete;
        RichTranscript& operator=(const RichTranscript&) = delete;

        RichTranscript(RichTranscript&&) = delete;
        RichTranscript& operator=(RichTranscript&&) = delete;

        void appendUserMessage(
            std::string_view text);

        void startAssistantResponse();

        void appendAssistantText(
            std::string_view text);

        void finishAssistantResponse(
            std::string_view authoritativeText);

        void appendError(
            std::string_view text);

        void clear();

        // Return a plain UTF-8 export of the visible conversation. This is used by
        // Ctrl+Shift+C so rich presentation never traps the user's text in the UI.
        [[nodiscard]]
        std::string copyableText() const;

        // Positive delta scrolls down toward newer content; negative delta scrolls up.
        void scrollBy(
            float deltaPixels);

        // Draw into one transcript viewport. The class manages its own clipping,
        // vertical scroll position, and scrollbar inside this rectangle.
        void render(
            int x,
            int y,
            int width,
            int height);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace rose::ui
