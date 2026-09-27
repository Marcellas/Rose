#pragma once

#include "artifacts/Artifact.h"

#include <memory>

struct SDL_Renderer;
struct TTF_Font;
struct TTF_TextEngine;

namespace rose::ui
{

    // -------------------------------------------------------------------------
    // ArtifactCardStack
    // -------------------------------------------------------------------------
    //
    // UI-only presentation for files Rose produced during the current session.
    //
    // The stack owns SDL_ttf text objects and image preview textures, but it does
    // NOT own artifact files on disk. ArtifactStore owns those file lifetimes.
    //
    // Keeping this separate from SdlChatWindow prevents the composer from growing
    // into another presentation god-object and lets us preserve the selectable
    // transcript while restoring artifact previews and click behavior.
    class ArtifactCardStack final
    {
    public:
        ArtifactCardStack(
            SDL_Renderer& renderer,
            TTF_TextEngine& textEngine,
            TTF_Font& font);

        ~ArtifactCardStack();

        ArtifactCardStack(const ArtifactCardStack&) = delete;
        ArtifactCardStack& operator=(const ArtifactCardStack&) = delete;
        ArtifactCardStack(ArtifactCardStack&&) = delete;
        ArtifactCardStack& operator=(ArtifactCardStack&&) = delete;

        void appendArtifact(
            artifacts::Artifact artifact);

        void clear();

        [[nodiscard]]
        bool empty() const noexcept;

        // Returns the height the panel would like to use, bounded by maxHeight.
        // Width matters because card labels wrap.
        [[nodiscard]]
        int preferredHeight(
            int width,
            int maxHeight);

        void scrollBy(
            float deltaPixels);

        [[nodiscard]]
        bool containsPoint(
            float x,
            float y) const noexcept;

        // Left-click opens the artifact. Right-click reveals it in Explorer.
        // Returns true when an artifact card consumed the click.
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
