#pragma once

#include "ui/ResponseDocument.h"

#include <filesystem>
#include <memory>

struct SDL_Renderer;
struct TTF_TextEngine;

namespace rose::ui
{

    // Cached, renderer-specific layout of one semantic response document.
    //
    // It owns all TTF_Text objects and LaTeX textures needed to draw the response.
    // Therefore the SDL renderer and text engine used to create it must outlive it.
    class RichResponseLayout final
    {
    public:
        RichResponseLayout() noexcept;
        ~RichResponseLayout();

        RichResponseLayout(RichResponseLayout&&) noexcept;
        RichResponseLayout& operator=(RichResponseLayout&&) noexcept;

        RichResponseLayout(const RichResponseLayout&) = delete;
        RichResponseLayout& operator=(const RichResponseLayout&) = delete;

        [[nodiscard]]
        int height() const noexcept;

        [[nodiscard]]
        explicit operator bool() const noexcept;

    private:
        friend class RichResponseRenderer;

        struct Impl;

        explicit RichResponseLayout(
            std::unique_ptr<Impl> impl) noexcept;

        std::unique_ptr<Impl> impl_;
    };


    // -----------------------------------------------------------------------------
    // RichResponseRenderer
    // -----------------------------------------------------------------------------
    //
    // Main-thread-only presentation renderer for parsed model responses.
    //
    // OWNERSHIP:
    //     - borrows SDL_Renderer and TTF_TextEngine
    //     - owns its presentation fonts
    //     - owns MathRenderer (and therefore the process-local MicroTeX context)
    //
    // DATA FLOW:
    //     ResponseDocument -> layout() once -> RichResponseLayout -> draw() many times
    //
    // Window resizing invalidates width-dependent layouts; ordinary scrolling does not.
    class RichResponseRenderer final
    {
    public:
        RichResponseRenderer(
            SDL_Renderer& renderer,
            TTF_TextEngine& textEngine,
            std::filesystem::path fontPath,
            std::filesystem::path microTexResourceRoot);

        ~RichResponseRenderer();

        RichResponseRenderer(const RichResponseRenderer&) = delete;
        RichResponseRenderer& operator=(const RichResponseRenderer&) = delete;

        RichResponseRenderer(RichResponseRenderer&&) = delete;
        RichResponseRenderer& operator=(RichResponseRenderer&&) = delete;

        [[nodiscard]]
        RichResponseLayout layout(
            const ResponseDocument& document,
            int width) const;

        void draw(
            const RichResponseLayout& layout,
            float x,
            float y) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace rose::ui
