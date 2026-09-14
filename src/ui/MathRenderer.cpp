#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ui/MathRenderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <gdiplus.h>

// Windows/GDI also defines TRANSPARENT as a macro.
// MicroTeX uses the same identifier internally.
#ifdef TRANSPARENT
#undef TRANSPARENT
#endif

#include "latex.h"
#include "platform/gdi_win/graphic_win32.h"
#include "utils/utf.h"

#pragma comment(lib, "gdiplus.lib")

#endif


namespace rose::ui
{

    namespace
    {

        struct TextureDeleter
        {
            void operator()(
                SDL_Texture* texture) const noexcept
            {
                if (texture != nullptr)
                {
                    SDL_DestroyTexture(
                        texture);
                }
            }
        };


        using TexturePtr =
            std::unique_ptr<
                SDL_Texture,
                TextureDeleter>;


        [[nodiscard]]
        std::string trimCopy(
            const std::string_view text)
        {
            std::size_t begin{ 0 };

            while (
                begin < text.size()
                && (
                    text[begin] == ' '
                    || text[begin] == '\t'
                    || text[begin] == '\r'
                    || text[begin] == '\n'))
            {
                ++begin;
            }


            std::size_t end{
                text.size()
            };

            while (
                end > begin
                && (
                    text[end - 1] == ' '
                    || text[end - 1] == '\t'
                    || text[end - 1] == '\r'
                    || text[end - 1] == '\n'))
            {
                --end;
            }


            return std::string{
                text.substr(
                    begin,
                    end - begin)
            };
        }


#ifdef _WIN32

        [[nodiscard]]
        std::wstring makeFormula(
            const std::string_view latex,
            const bool displayStyle)
        {
            const std::string trimmed =
                trimCopy(
                    latex);


            if (trimmed.empty())
            {
                return {};
            }


            if (!displayStyle)
            {
                // MicroTeX needs explicit math mode for operators such as
                // superscripts/subscripts (for example mc^2 or G_{\\mu\\nu}).
                //
                // ResponseParser deliberately removes the Markdown/LaTeX inline
                // delimiters before storing an InlineStyle::Math span, so restore
                // a single-dollar math-mode wrapper here at the renderer boundary.
                // This keeps parsing semantic and keeps MicroTeX-specific syntax
                // knowledge inside MathRenderer.
                const std::string inlineFormula =
                    std::string{ "$" }
                    + trimmed
                    + "$";

                return tex::utf82wide(
                    inlineFormula);
            }


            const bool alreadyDisplayDelimited =
                trimmed.starts_with("$$")
                || trimmed.starts_with("\\[");


            const std::string display =
                alreadyDisplayDelimited
                    ? trimmed
                    : std::string{
                        "$$"
                    }
                    + trimmed
                    + "$$";


            return tex::utf82wide(
                display);
        }

#endif

    } // namespace


    // =========================================================================
    // MathTexture
    // =========================================================================

    struct MathTexture::Impl
    {
        TexturePtr texture;

        int width{ 0 };
        int height{ 0 };
    };


    MathTexture::MathTexture() noexcept = default;

    MathTexture::~MathTexture() = default;

    MathTexture::MathTexture(
        MathTexture&& other) noexcept = default;

    MathTexture& MathTexture::operator=(
        MathTexture&& other) noexcept = default;


    MathTexture::MathTexture(
        std::unique_ptr<Impl> impl) noexcept
        : impl_{
            std::move(
                impl)
        }
    {
    }


    SDL_Texture* MathTexture::texture() const noexcept
    {
        return
            impl_
                ? impl_->texture.get()
                : nullptr;
    }


    int MathTexture::width() const noexcept
    {
        return
            impl_
                ? impl_->width
                : 0;
    }


    int MathTexture::height() const noexcept
    {
        return
            impl_
                ? impl_->height
                : 0;
    }


    MathTexture::operator bool() const noexcept
    {
        return texture() != nullptr;
    }


    // =========================================================================
    // MathRenderer
    // =========================================================================

    struct MathRenderer::Impl
    {
        SDL_Renderer& renderer_;

#ifdef _WIN32
        ULONG_PTR gdiplusToken_{ 0 };

        bool microTexInitialized_{ false };
#endif


        explicit Impl(
            SDL_Renderer& renderer,
            std::filesystem::path microTexResourceRoot)
            : renderer_{
                renderer
            }
        {
#ifdef _WIN32
            if (microTexResourceRoot.empty())
            {
                throw std::invalid_argument{
                    "MathRenderer requires a MicroTeX resource directory."
                };
            }


            const std::filesystem::path absoluteResourceRoot =
                std::filesystem::absolute(
                    microTexResourceRoot);


            if (!std::filesystem::exists(
                absoluteResourceRoot))
            {
                throw std::runtime_error{
                    std::string{
                        "MicroTeX resource directory does not exist: "
                    }
                    + absoluteResourceRoot.string()
                };
            }


            Gdiplus::GdiplusStartupInput startupInput;

            const Gdiplus::Status startupStatus =
                Gdiplus::GdiplusStartup(
                    &gdiplusToken_,
                    &startupInput,
                    nullptr);


            if (startupStatus != Gdiplus::Ok)
            {
                throw std::runtime_error{
                    "Could not initialize GDI+ for Rose's math renderer."
                };
            }


            try
            {
                tex::LaTeX::init(
                    absoluteResourceRoot.string());

                microTexInitialized_ = true;
            }
            catch (...)
            {
                Gdiplus::GdiplusShutdown(
                    gdiplusToken_);

                gdiplusToken_ = 0;

                throw;
            }
#else
            (void)microTexResourceRoot;

            throw std::runtime_error{
                "Rose's current native LaTeX renderer is implemented for Windows only."
            };
#endif
        }


        ~Impl() noexcept
        {
#ifdef _WIN32
            if (microTexInitialized_)
            {
                try
                {
                    tex::LaTeX::release();
                }
                catch (...)
                {
                    // Destructors must not propagate failures from a presentation
                    // dependency during application shutdown.
                }
            }


            if (gdiplusToken_ != 0)
            {
                Gdiplus::GdiplusShutdown(
                    gdiplusToken_);
            }
#endif
        }


        [[nodiscard]]
        MathTexture renderMath(
            const std::string_view latex,
            const int maximumWidth,
            const float textSize,
            const bool displayStyle) const
        {
#ifdef _WIN32
            if (maximumWidth <= 0)
            {
                throw std::invalid_argument{
                    "MathRenderer maximumWidth must be greater than zero."
                };
            }


            if (textSize <= 0.0f)
            {
                throw std::invalid_argument{
                    "MathRenderer textSize must be greater than zero."
                };
            }


            const std::wstring formula =
                makeFormula(
                    latex,
                    displayStyle);


            if (formula.empty())
            {
                return {};
            }


            // MicroTeX returns a raw TeXRender pointer. Take ownership immediately
            // so parser/layout exceptions later cannot leak it.
            std::unique_ptr<tex::TeXRender> render{
                tex::LaTeX::parse(
                    formula,
                    maximumWidth,
                    textSize,
                    textSize * 0.30f,
                    0xFFF5F5F8u)
            };


            if (!render)
            {
                throw std::runtime_error{
                    "MicroTeX returned no render object."
                };
            }


            const int width =
                render->getWidth();

            const int height =
                render->getHeight();


            // A malformed/untrusted formula must never be allowed to request an
            // absurd backing bitmap. These are presentation limits, not model
            // context limits.
            constexpr int maximumTextureDimension{
                8192
            };


            if (
                width <= 0
                || height <= 0
                || width > maximumTextureDimension
                || height > maximumTextureDimension)
            {
                throw std::runtime_error{
                    "Rendered LaTeX formula has invalid or excessive dimensions."
                };
            }


            Gdiplus::Bitmap bitmap{
                width,
                height,
                PixelFormat32bppARGB
            };


            Gdiplus::Graphics graphics{
                &bitmap
            };


            graphics.Clear(
                Gdiplus::Color{
                    0,
                    0,
                    0,
                    0
                });


            tex::Graphics2D_win32 graphics2D{
                &graphics
            };


            render->draw(
                graphics2D,
                0,
                0);


            Gdiplus::Rect lockRect{
                0,
                0,
                width,
                height
            };


            Gdiplus::BitmapData bitmapData{};


            const Gdiplus::Status lockStatus =
                bitmap.LockBits(
                    &lockRect,
                    Gdiplus::ImageLockModeRead,
                    PixelFormat32bppARGB,
                    &bitmapData);


            if (lockStatus != Gdiplus::Ok)
            {
                throw std::runtime_error{
                    "Could not read the GDI+ math bitmap."
                };
            }


            struct BitmapUnlockGuard
            {
                Gdiplus::Bitmap& bitmap;
                Gdiplus::BitmapData& data;

                ~BitmapUnlockGuard()
                {
                    bitmap.UnlockBits(
                        &data);
                }
            } unlockGuard{
                bitmap,
                bitmapData
            };


            const int sourceStride =
                bitmapData.Stride;

            const int absoluteSourceStride =
                std::abs(
                    sourceStride);


            std::vector<std::uint8_t> pixels(
                static_cast<std::size_t>(
                    width)
                * static_cast<std::size_t>(
                    height)
                * 4u);


            const auto* sourceBase =
                static_cast<const std::uint8_t*>(
                    bitmapData.Scan0);


            for (
                int y = 0;
                y < height;
                ++y)
            {
                const int sourceRowIndex =
                    sourceStride >= 0
                        ? y
                        : height - 1 - y;


                const std::uint8_t* sourceRow =
                    sourceBase
                    + static_cast<std::ptrdiff_t>(
                        sourceRowIndex)
                    * absoluteSourceStride;


                std::uint8_t* destinationRow =
                    pixels.data()
                    + static_cast<std::size_t>(
                        y)
                    * static_cast<std::size_t>(
                        width)
                    * 4u;


                std::memcpy(
                    destinationRow,
                    sourceRow,
                    static_cast<std::size_t>(
                        width)
                    * 4u);
            }


            SDL_Surface* surface =
                SDL_CreateSurfaceFrom(
                    width,
                    height,
                    SDL_PIXELFORMAT_BGRA32,
                    pixels.data(),
                    width * 4);


            if (surface == nullptr)
            {
                throw std::runtime_error{
                    std::string{
                        "Could not create SDL surface for LaTeX formula: "
                    }
                    + SDL_GetError()
                };
            }


            struct SurfaceGuard
            {
                SDL_Surface* surface;

                ~SurfaceGuard()
                {
                    if (surface != nullptr)
                    {
                        SDL_DestroySurface(
                            surface);
                    }
                }
            } surfaceGuard{
                surface
            };


            TexturePtr texture{
                SDL_CreateTextureFromSurface(
                    &renderer_,
                    surface)
            };


            if (!texture)
            {
                throw std::runtime_error{
                    std::string{
                        "Could not create SDL texture for LaTeX formula: "
                    }
                    + SDL_GetError()
                };
            }


            if (!SDL_SetTextureBlendMode(
                texture.get(),
                SDL_BLENDMODE_BLEND))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not enable alpha blending for LaTeX texture: "
                    }
                    + SDL_GetError()
                };
            }


            auto textureImpl =
                std::make_unique<MathTexture::Impl>();

            textureImpl->texture =
                std::move(
                    texture);

            textureImpl->width =
                width;

            textureImpl->height =
                height;


            return MathTexture{
                std::move(
                    textureImpl)
            };
#else
            (void)latex;
            (void)maximumWidth;
            (void)textSize;
            (void)displayStyle;

            throw std::runtime_error{
                "Rose's current native LaTeX renderer is implemented for Windows only."
            };
#endif
        }
    };


    MathRenderer::MathRenderer(
        SDL_Renderer& renderer,
        std::filesystem::path microTexResourceRoot)
        : impl_{
            std::make_unique<Impl>(
                renderer,
                std::move(
                    microTexResourceRoot))
        }
    {
    }


    MathRenderer::~MathRenderer() = default;


    MathTexture MathRenderer::renderDisplayMath(
        const std::string_view latex,
        const int maximumWidth,
        const float textSize) const
    {
        return impl_->renderMath(
            latex,
            maximumWidth,
            textSize,
            true);
    }


    MathTexture MathRenderer::renderInlineMath(
        const std::string_view latex,
        const int maximumWidth,
        const float textSize) const
    {
        return impl_->renderMath(
            latex,
            maximumWidth,
            textSize,
            false);
    }

} // namespace rose::ui
