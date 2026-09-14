#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

struct SDL_Renderer;
struct SDL_Texture;

namespace rose::ui
{

    // MathTexture
    // -------------------------------------------------------------------------
    // Owns one SDL texture containing a rasterized LaTeX formula.
    //
    // The texture is created for one specific SDL_Renderer. That renderer must
    // therefore outlive this object.
    class MathTexture final
    {
    public:
        MathTexture() noexcept;
        ~MathTexture();

        MathTexture(MathTexture&& other) noexcept;
        MathTexture& operator=(MathTexture&& other) noexcept;

        MathTexture(const MathTexture&) = delete;
        MathTexture& operator=(const MathTexture&) = delete;

        [[nodiscard]]
        SDL_Texture* texture() const noexcept;

        [[nodiscard]]
        int width() const noexcept;

        [[nodiscard]]
        int height() const noexcept;

        [[nodiscard]]
        explicit operator bool() const noexcept;

    private:
        friend class MathRenderer;

        struct Impl;

        explicit MathTexture(
            std::unique_ptr<Impl> impl) noexcept;

        std::unique_ptr<Impl> impl_;
    };


    // MathRenderer
    // -------------------------------------------------------------------------
    // Native Windows LaTeX -> SDL texture adapter.
    //
    // MicroTeX performs the actual TeX parsing and mathematical layout. On
    // Windows its GDI+ backend renders into an in-memory bitmap; this adapter
    // copies that bitmap into a normal SDL texture so the rest of Rose remains
    // renderer-agnostic.
    //
    // OWNERSHIP:
    //     - borrows SDL_Renderer
    //     - owns GDI+ process token for this renderer lifetime
    //     - owns MicroTeX's initialized global context for Rose v0.1
    //
    // Rose currently creates one graphical chat frontend, so one MathRenderer
    // is sufficient. If multiple simultaneous math renderers are ever required,
    // move MicroTeX/GDI+ process initialization into an application runtime.
    class MathRenderer final
    {
    public:
        MathRenderer(
            SDL_Renderer& renderer,
            std::filesystem::path microTexResourceRoot);

        ~MathRenderer();

        MathRenderer(const MathRenderer&) = delete;
        MathRenderer& operator=(const MathRenderer&) = delete;

        MathRenderer(MathRenderer&&) = delete;
        MathRenderer& operator=(MathRenderer&&) = delete;

        // Rasterize one display-style LaTeX formula.
        //
        // latex should contain the formula itself. Delimiters such as $$...$$
        // or \[...\] are optional; Rose adds display delimiters when absent.
        [[nodiscard]]
        MathTexture renderDisplayMath(
            std::string_view latex,
            int maximumWidth,
            float textSize = 24.0f) const;

        // Rasterize one inline-style LaTeX formula. Unlike display math, the
        // resulting texture uses the formula's natural width (up to maximumWidth)
        // so it can participate in a mixed text line.
        [[nodiscard]]
        MathTexture renderInlineMath(
            std::string_view latex,
            int maximumWidth,
            float textSize = 18.0f) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace rose::ui
