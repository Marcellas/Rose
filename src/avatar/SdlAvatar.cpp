#include "avatar/SdlAvatar.h"

#include "platform/SdlRuntime.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace rose::avatar
{

    namespace
    {

        // -----------------------------------------------------------------------------
        // SdlRuntime
        // -----------------------------------------------------------------------------
        //
        // SDL initialization is process-level state.
        //
        // For the current avatar sandbox there is one SDL consumer, so this small
        // function-local RAII object is sufficient.
        //
        // FUTURE:
        // Once Rose's full desktop application uses SDL for multiple systems, move this
        // into a dedicated application/platform runtime object.
        class SdlRuntime final
        {
        public:
            SdlRuntime()
            {
                if (!SDL_Init(SDL_INIT_VIDEO))
                {
                    throw std::runtime_error{
                        std::string{
                            "SDL video initialization failed: "
                        }
                        + SDL_GetError()
                    };
                }
            }


            ~SdlRuntime()
            {
                SDL_Quit();
            }


            SdlRuntime(const SdlRuntime&) = delete;
            SdlRuntime& operator=(const SdlRuntime&) = delete;
        };

    } // namespace


    SdlAvatar::SdlAvatar(
        platform::SdlRuntime& runtime,
        const int width,
        const int height)
        : runtime_{ runtime }
    {
        if (width <= 0 || height <= 0)
        {
            throw std::invalid_argument{
                "SdlAvatar dimensions must be greater than zero."
            };
        }

        SDL_Window* rawWindow{ nullptr };
        SDL_Renderer* rawRenderer{ nullptr };

        const SDL_WindowFlags flags =
            SDL_WINDOW_BORDERLESS
            | SDL_WINDOW_ALWAYS_ON_TOP
            | SDL_WINDOW_TRANSPARENT;


        if (!SDL_CreateWindowAndRenderer(
            "Rose Avatar",
            width,
            height,
            flags,
            &rawWindow,
            &rawRenderer))
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose avatar window: "
                }
                + SDL_GetError()
            };
        }


        window_.reset(
            rawWindow);

        renderer_.reset(
            rawRenderer);
    }


    SdlAvatar::~SdlAvatar() = default;


    void SdlAvatar::setState(
        const AvatarState state)
    {
        // No SDL calls here.
        //
        // This is intentionally just an atomic state publication so a future
        // inference thread can safely notify the avatar.
        state_.store(
            state,
            std::memory_order_relaxed);
    }

    bool SdlAvatar::handleEvent(
        const SDL_Event& event)
    {
        const SDL_WindowID windowId =
            SDL_GetWindowID(
                window_.get());


        switch (event.type)
        {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == windowId)
            {
                return false;
            }

            break;


        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            // Ignore mouse events belonging to another SDL window before doing any
            // coordinate-dependent work.
            if (event.button.windowID != windowId)
            {
                break;
            }


            // Dragging may begin only when the user actually presses an opaque part
            // of Rose's rendered sprite.
            //
            // Clicking transparent air inside the rectangular SDL window is not
            // considered interacting with Rose.
            if (
                event.button.button == SDL_BUTTON_LEFT
                && isSpritePixelAt(
                    event.button.x,
                    event.button.y))
            {
                dragging_ = true;


                // Global desktop coordinates remain stable while the window moves.
                SDL_GetGlobalMouseState(
                    &dragStartGlobalMouseX_,
                    &dragStartGlobalMouseY_);


                SDL_GetWindowPosition(
                    window_.get(),
                    &dragStartWindowX_,
                    &dragStartWindowY_);
            }


            break;
        }


        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.windowID != windowId)
            {
                break;
            }


            if (event.button.button == SDL_BUTTON_LEFT)
            {
                dragging_ = false;
            }

            break;


        case SDL_EVENT_MOUSE_MOTION:
            if (
                event.motion.windowID == windowId
                && dragging_)
            {
                float currentGlobalMouseX{
                    0.0f
                };

                float currentGlobalMouseY{
                    0.0f
                };


                SDL_GetGlobalMouseState(
                    &currentGlobalMouseX,
                    &currentGlobalMouseY);


                const int newX =
                    dragStartWindowX_
                    + static_cast<int>(
                        currentGlobalMouseX
                        - dragStartGlobalMouseX_);


                const int newY =
                    dragStartWindowY_
                    + static_cast<int>(
                        currentGlobalMouseY
                        - dragStartGlobalMouseY_);


                SDL_SetWindowPosition(
                    window_.get(),
                    newX,
                    newY);
            }

            break;


        default:
            break;
        }


        return true;
    }


    void SdlAvatar::render()
    {
        const AvatarState state =
            state_.load(
                std::memory_order_relaxed);

        const auto now =
            std::chrono::steady_clock::now();


        if (state != lastRenderedState_)
        {
            lastRenderedState_ =
                state;

            animationStart_ =
                now;
        }

        const float elapsedSeconds =
            std::chrono::duration<float>(
                now - animationStart_)
            .count();


        const AvatarTransform transform =
            calculateTransform(
                state,
                elapsedSeconds);

        // -------------------------------------------------------------------------
        // Placeholder rendering
        // -------------------------------------------------------------------------
        //
        // These simple colors/shapes exist only to prove that state changes reach
        // the graphical presentation layer.
        //
        // They will be replaced by Rose's actual sprite/animation system.
        switch (state)
        {
        case AvatarState::Idle:
            SDL_SetRenderDrawColor(
                renderer_.get(),
                55,
                45,
                75,
                255);
            break;

        case AvatarState::Listening:
            SDL_SetRenderDrawColor(
                renderer_.get(),
                40,
                90,
                120,
                255);
            break;

        case AvatarState::Thinking:
            SDL_SetRenderDrawColor(
                renderer_.get(),
                95,
                65,
                135,
                255);
            break;

        case AvatarState::Speaking:
            SDL_SetRenderDrawColor(
                renderer_.get(),
                120,
                75,
                105,
                255);
            break;

        case AvatarState::Working:
            SDL_SetRenderDrawColor(
                renderer_.get(),
                80,
                100,
                70,
                255);
            break;

        case AvatarState::Notification:
            SDL_SetRenderDrawColor(
                renderer_.get(),
                125,
                105,
                55,
                255);
            break;

        case AvatarState::Confused:
            SDL_SetRenderDrawColor(
                renderer_.get(),
                120,
                55,
                55,
                255);
            break;
        }

        SDL_SetRenderDrawColor(
            renderer_.get(),
            0,
            0,
            0,
            0);


        SDL_RenderClear(
            renderer_.get());

        if (spriteTexture_)
        {
            int outputWidth{ 0 };
            int outputHeight{ 0 };


            if (!SDL_GetRenderOutputSize(
                renderer_.get(),
                &outputWidth,
                &outputHeight))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not query Rose avatar render size: "
                    }
                    + SDL_GetError()
                };
            }


            const float availableWidth =
                static_cast<float>(
                    outputWidth);

            const float availableHeight =
                static_cast<float>(
                    outputHeight);

            // Preserve the original image aspect ratio.
            const float baseScale =
                std::min(
                    availableWidth / spriteWidth_,
                    availableHeight / spriteHeight_);


            const float animatedScale =
                baseScale
                * transform.scale;


            const float renderedWidth =
                spriteWidth_
                * animatedScale;


            const float renderedHeight =
                spriteHeight_
                * animatedScale;


            // Center Rose inside the current avatar window.
            const SDL_FRect destination{
                (availableWidth - renderedWidth)
                    * 0.5f
                    + transform.offsetX,

                (availableHeight - renderedHeight)
                    * 0.5f
                    + transform.offsetY,

                renderedWidth,

                renderedHeight
            };

            renderedPose_.x =
                destination.x;

            renderedPose_.y =
                destination.y;

            renderedPose_.width =
                destination.w;

            renderedPose_.height =
                destination.h;

            renderedPose_.rotationDegrees =
                transform.rotationDegrees;

            renderedPose_.valid =
                true;

            if (!SDL_RenderTextureRotated(
                renderer_.get(),
                spriteTexture_.get(),
                nullptr,
                &destination,
                transform.rotationDegrees,
                nullptr,
                SDL_FLIP_NONE))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not render animated Rose avatar: "
                    }
                    + SDL_GetError()
                };
            }
        }

        SDL_RenderPresent(
            renderer_.get());
    }


    void SdlAvatar::WindowDeleter::operator()(
        SDL_Window* window) const noexcept
    {
        if (window != nullptr)
        {
            SDL_DestroyWindow(
                window);
        }
    }


    void SdlAvatar::RendererDeleter::operator()(
        SDL_Renderer* renderer) const noexcept
    {
        if (renderer != nullptr)
        {
            SDL_DestroyRenderer(
                renderer);
        }
    }

    void SdlAvatar::TextureDeleter::operator()(
        SDL_Texture* texture) const noexcept
    {
        if (texture != nullptr)
        {
            SDL_DestroyTexture(
                texture);
        }
    }

    struct SurfaceDeleter
    {
        void operator()(
            SDL_Surface* surface) const noexcept
        {
            if (surface != nullptr)
            {
                SDL_DestroySurface(
                    surface);
            }
        }
    };

    using SurfacePtr =
        std::unique_ptr<
        SDL_Surface,
        SurfaceDeleter>;

    void SdlAvatar::loadSprite(
        const std::filesystem::path& path)
    {
        const std::filesystem::path absolutePath =
            std::filesystem::absolute(
                path);


        if (!std::filesystem::exists(
            absolutePath))
        {
            throw std::runtime_error{
                std::string{
                    "Rose avatar image does not exist: "
                }
                + absolutePath.string()
            };
        }


        const std::string pathString =
            absolutePath.string();


        SurfacePtr surface{
            IMG_Load(
                pathString.c_str())
        };


        if (!surface)
        {
            throw std::runtime_error{
                std::string{
                    "Could not load Rose avatar surface '"
                }
                + pathString
                + "': "
                + SDL_GetError()
            };
        }


        spritePixelWidth_ =
            surface->w;

        spritePixelHeight_ =
            surface->h;


        if (
            spritePixelWidth_ <= 0
            || spritePixelHeight_ <= 0)
        {
            throw std::runtime_error{
                "Rose avatar image has invalid dimensions."
            };
        }

        spriteAlpha_.resize(
            static_cast<std::size_t>(
                spritePixelWidth_
                * spritePixelHeight_));


        for (int y = 0;
            y < spritePixelHeight_;
            ++y)
        {
            for (int x = 0;
                x < spritePixelWidth_;
                ++x)
            {
                Uint8 alpha{ 255 };


                if (!SDL_ReadSurfacePixel(
                    surface.get(),
                    x,
                    y,
                    nullptr,
                    nullptr,
                    nullptr,
                    &alpha))
                {
                    throw std::runtime_error{
                        std::string{
                            "Could not read Rose avatar alpha: "
                        }
                        + SDL_GetError()
                    };
                }


                const std::size_t index =
                    static_cast<std::size_t>(
                        y * spritePixelWidth_
                        + x);


                spriteAlpha_[index] =
                    alpha;
            }
        }

        TexturePtr newTexture{
    SDL_CreateTextureFromSurface(
        renderer_.get(),
        surface.get())
        };


        if (!newTexture)
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose avatar texture: "
                }
                + SDL_GetError()
            };
        }

        // Only replace the existing image after the entire new image has loaded
        // successfully.
        //
        // This gives us a strong exception guarantee: a failed replacement never
        // destroys the currently usable sprite.

        spriteTexture_ =
            std::move(
                newTexture);


        spriteWidth_ =
            static_cast<float>(
                spritePixelWidth_);

        spriteHeight_ =
            static_cast<float>(
                spritePixelHeight_);

        int windowWidth{ 0 };
        int windowHeight{ 0 };


        if (!SDL_GetWindowSize(
            window_.get(),
            &windowWidth,
            &windowHeight))
        {
            throw std::runtime_error{
                std::string{
                    "Could not query Rose window size: "
                }
                + SDL_GetError()
            };
        }


        SurfacePtr windowShape{
            SDL_CreateSurface(
                windowWidth,
                windowHeight,
                SDL_PIXELFORMAT_RGBA32)
        };


        if (!windowShape)
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose window shape surface: "
                }
                + SDL_GetError()
            };
        }

        const float scale =
            std::min(
                static_cast<float>(
                    windowWidth)
                / spriteWidth_,

                static_cast<float>(
                    windowHeight)
                / spriteHeight_);


        const int shapeWidth =
            static_cast<int>(
                spriteWidth_
                * scale);


        const int shapeHeight =
            static_cast<int>(
                spriteHeight_
                * scale);


        const SDL_Rect destination{
            (windowWidth - shapeWidth)
                / 2,

            (windowHeight - shapeHeight)
                / 2,

            shapeWidth,
            shapeHeight
        };

        if (!SDL_BlitSurfaceScaled(
            surface.get(),
            nullptr,
            windowShape.get(),
            &destination,
            SDL_SCALEMODE_LINEAR))
        {
            throw std::runtime_error{
                std::string{
                    "Could not build Rose window silhouette: "
                }
                + SDL_GetError()
            };
        }

        if (!SDL_SetWindowShape(
            window_.get(),
            windowShape.get()))
        {
            throw std::runtime_error{
                std::string{
                    "Could not apply Rose window silhouette: "
                }
                + SDL_GetError()
            };
        }

    }

    bool SdlAvatar::isSpritePixelAt(
        const float windowX,
        const float windowY) const noexcept
    {
        if (
            !renderedPose_.valid
            || spriteAlpha_.empty()
            || spritePixelWidth_ <= 0
            || spritePixelHeight_ <= 0)
        {
            return false;
        }


        // Translate the point so the rendered sprite center becomes the origin.
        const float centerX =
            renderedPose_.x
            + renderedPose_.width
            * 0.5f;


        const float centerY =
            renderedPose_.y
            + renderedPose_.height
            * 0.5f;


        float x =
            windowX
            - centerX;


        float y =
            windowY
            - centerY;


        // Undo the rendered rotation.
        //
        // Rendering rotated Rose clockwise, so hit-testing rotates the mouse point
        // in the opposite direction.
        constexpr double pi{
            3.14159265358979323846
        };


        const double radians =
            -renderedPose_.rotationDegrees
            * pi
            / 180.0;


        const float cosine =
            static_cast<float>(
                std::cos(
                    radians));


        const float sine =
            static_cast<float>(
                std::sin(
                    radians));


        const float unrotatedX =
            x * cosine
            - y * sine;


        const float unrotatedY =
            x * sine
            + y * cosine;


        // Convert back from center-relative coordinates into the destination rect.
        const float destinationX =
            unrotatedX
            + renderedPose_.width
            * 0.5f;


        const float destinationY =
            unrotatedY
            + renderedPose_.height
            * 0.5f;


        if (
            destinationX < 0.0f
            || destinationY < 0.0f
            || destinationX >= renderedPose_.width
            || destinationY >= renderedPose_.height)
        {
            return false;
        }


        // Map destination coordinates into original source-image coordinates.
        const int sourceX =
            static_cast<int>(
                destinationX
                / renderedPose_.width
                * static_cast<float>(
                    spritePixelWidth_));


        const int sourceY =
            static_cast<int>(
                destinationY
                / renderedPose_.height
                * static_cast<float>(
                    spritePixelHeight_));


        if (
            sourceX < 0
            || sourceY < 0
            || sourceX >= spritePixelWidth_
            || sourceY >= spritePixelHeight_)
        {
            return false;
        }


        const std::size_t index =
            static_cast<std::size_t>(
                sourceY
                * spritePixelWidth_
                + sourceX);


        // Ignore almost-transparent antialiasing fringe pixels.
        constexpr std::uint8_t interactionAlphaThreshold{
            16
        };


        return
            spriteAlpha_[index]
            >= interactionAlphaThreshold;
    }

    SdlAvatar::AvatarTransform
        SdlAvatar::calculateTransform(
            const AvatarState state,
            const float elapsedSeconds) const
    {
        AvatarTransform transform{};


        // Keep a local Pi constant so this code does not depend on non-standard
        // platform math macros.
        constexpr float pi{
            3.14159265358979323846f
        };


        switch (state)
        {
        case AvatarState::Idle:
        {
            // Very slow breathing / floating motion.
            const float wave =
                std::sin(
                    elapsedSeconds
                    * 1.5f);


            transform.offsetY =
                wave * 2.0f;


            transform.scale =
                1.0f
                + wave * 0.004f;

            break;
        }


        case AvatarState::Listening:
        {
            // A slightly more attentive, energetic motion.
            const float wave =
                std::sin(
                    elapsedSeconds
                    * 3.0f);


            transform.offsetY =
                wave * 1.5f;


            transform.rotationDegrees =
                static_cast<double>(
                    wave * 0.6f);

            break;
        }


        case AvatarState::Thinking:
        {
            // Gentle floating/swaying motion.
            transform.offsetX =
                std::sin(
                    elapsedSeconds
                    * 1.1f)
                * 2.5f;


            transform.offsetY =
                std::sin(
                    elapsedSeconds
                    * 1.7f)
                * 3.0f;


            transform.rotationDegrees =
                static_cast<double>(
                    std::sin(
                        elapsedSeconds
                        * 0.9f)
                    * 1.3f);

            break;
        }


        case AvatarState::Speaking:
        {
            // Small rhythmic pulse.
            //
            // This is intentionally subtle. Later the speaking state can be driven
            // by actual voice amplitude instead of a fixed sine wave.
            const float wave =
                std::sin(
                    elapsedSeconds
                    * pi
                    * 4.0f);


            transform.scale =
                1.0f
                + wave * 0.008f;


            transform.offsetY =
                wave * 1.0f;

            break;
        }


        case AvatarState::Working:
        {
            // Slightly faster motion than Thinking, suggesting activity.
            transform.offsetX =
                std::sin(
                    elapsedSeconds
                    * 2.0f)
                * 1.5f;


            transform.offsetY =
                std::sin(
                    elapsedSeconds
                    * 2.7f)
                * 2.0f;

            break;
        }


        case AvatarState::Notification:
        {
            // Repeating small bounce for now.
            //
            // Later this can become a one-shot animation that returns to Idle.
            const float bounce =
                std::abs(
                    std::sin(
                        elapsedSeconds
                        * 5.0f));


            transform.offsetY =
                -bounce * 8.0f;

            break;
        }


        case AvatarState::Confused:
        {
            // Gentle left/right wobble.
            transform.rotationDegrees =
                static_cast<double>(
                    std::sin(
                        elapsedSeconds
                        * 4.0f)
                    * 3.0f);

            break;
        }
        }


        return transform;
    }

} // namespace rose::avatar