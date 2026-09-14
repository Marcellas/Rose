#pragma once

#include "artifacts/Artifact.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

struct SDL_Renderer;
struct TTF_TextEngine;

namespace rose::ui
{

    // Owns UI-only conversation presentation state, including generated artifacts.
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

        void appendArtifact(
            artifacts::Artifact artifact);

        void appendError(
            std::string_view text);

        void clear();

        [[nodiscard]]
        std::string copyableText() const;

        void scrollBy(
            float deltaPixels);

        // Returns true when the click was consumed by an artifact card.
        // Left-click opens the artifact. Right-click opens its containing folder.
        [[nodiscard]]
        bool handlePointerDown(
            float x,
            float y,
            bool revealFolder);

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
