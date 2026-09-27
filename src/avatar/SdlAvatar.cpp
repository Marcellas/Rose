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
        requestedState_.store(
            state,
            std::memory_order_relaxed);
    }

    void SdlAvatar::setPresentationOverride(
        const std::optional<AvatarState> state) noexcept
    {
        presentationOverride_ = state;
    }


    std::vector<AvatarInteractionEvent> SdlAvatar::takeInteractions()
    {
        std::vector<AvatarInteractionEvent> result;
        result.swap(pendingInteractions_);
        return result;
    }


    bool SdlAvatar::handleEvent(
        const SDL_Event& event)
    {
        const SDL_WindowID windowId = SDL_GetWindowID(window_.get());

        const auto queueInteraction =
            [this](const AvatarInteractionKind kind)
            {
                float globalX{ 0.0f };
                float globalY{ 0.0f };
                SDL_GetGlobalMouseState(&globalX, &globalY);
                pendingInteractions_.push_back(
                    AvatarInteractionEvent{
                        .kind = kind,
                        .globalX = globalX,
                        .globalY = globalY
                    });
            };

        switch (event.type)
        {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == windowId)
            {
                return false;
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.windowID != windowId)
            {
                break;
            }

            if (!isSpritePixelAt(event.button.x, event.button.y))
            {
                break;
            }

            if (event.button.button == SDL_BUTTON_RIGHT)
            {
                queueInteraction(AvatarInteractionKind::ContextMenuRequested);
                break;
            }

            if (event.button.button == SDL_BUTTON_LEFT)
            {
                pointerArmed_ = true;
                dragging_ = false;
                SDL_GetGlobalMouseState(&dragStartGlobalMouseX_, &dragStartGlobalMouseY_);
                SDL_GetWindowPosition(window_.get(), &dragStartWindowX_, &dragStartWindowY_);
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (event.motion.windowID == windowId && pointerArmed_)
            {
                float currentGlobalMouseX{ 0.0f };
                float currentGlobalMouseY{ 0.0f };
                SDL_GetGlobalMouseState(&currentGlobalMouseX, &currentGlobalMouseY);

                const float dx = currentGlobalMouseX - dragStartGlobalMouseX_;
                const float dy = currentGlobalMouseY - dragStartGlobalMouseY_;

                if (!dragging_ && std::hypot(dx, dy) >= dragThresholdPixels_)
                {
                    dragging_ = true;
                    queueInteraction(AvatarInteractionKind::DragStarted);
                }

                if (dragging_)
                {
                    SDL_SetWindowPosition(
                        window_.get(),
                        dragStartWindowX_ + static_cast<int>(dx),
                        dragStartWindowY_ + static_cast<int>(dy));
                }
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.windowID != windowId || event.button.button != SDL_BUTTON_LEFT)
            {
                break;
            }

            if (pointerArmed_)
            {
                if (dragging_)
                {
                    queueInteraction(AvatarInteractionKind::DragEnded);
                }
                else
                {
                    queueInteraction(AvatarInteractionKind::PrimaryClick);
                }
            }

            pointerArmed_ = false;
            dragging_ = false;
            break;

        default:
            break;
        }

        return true;
    }


    void SdlAvatar::render()
    {
        const AvatarState requestedState =
            presentationOverride_.value_or(
                requestedState_.load(
                    std::memory_order_relaxed));

        const auto now =
            AvatarPresentationTimeline::Clock::now();

        // The presentation timeline is intentionally allowed to lag the semantic
        // request just enough to finish a perceptible pose/state transition. This
        // never blocks Rose's worker thread; it changes only what the UI draws.
        const AvatarPresentationSample presentation =
            presentationTimeline_.update(
                requestedState,
                now,
                postureTransition_.has_value()
                    ? postureTransitionSeconds_
                    : 0.001f);

        const bool renderingPostureTransition =
            presentation.transitionActive()
            && postureTransition_.has_value();

        const AvatarState visualState =
            presentation.semanticState;

        const AvatarTransform transform =
            renderingPostureTransition
                ? AvatarTransform{}
                : calculateTransform(
                    visualState,
                    presentation.semanticElapsedSeconds);

        SDL_SetRenderDrawColor(
            renderer_.get(),
            0,
            0,
            0,
            0);

        SDL_RenderClear(
            renderer_.get());

        const AnimationClip* clip =
            renderingPostureTransition
                ? &postureTransition_.value()
                : animationFor(
                    visualState);

        SDL_Texture* texture =
            clip != nullptr
                ? clip->texture.get()
                : spriteTexture_.get();

        if (texture != nullptr)
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

            FrameBlend frameBlend{};

            if (clip != nullptr)
            {
                frameBlend =
                    renderingPostureTransition
                        ? frameBlendForProgress(
                            *clip,
                            presentation.postureProgress)
                        : frameBlendForElapsed(
                            *clip,
                            presentation.semanticElapsedSeconds);
            }

            const float visualWidth =
                clip != nullptr
                    ? static_cast<float>(
                        clip->frameWidth)
                    : spriteWidth_;

            const float visualHeight =
                clip != nullptr
                    ? static_cast<float>(
                        clip->frameHeight)
                    : spriteHeight_;

            if (
                visualWidth <= 0.0f
                || visualHeight <= 0.0f)
            {
                throw std::runtime_error{
                    "Rose avatar visual has invalid dimensions."
                };
            }

            const float availableWidth =
                static_cast<float>(
                    outputWidth);

            const float availableHeight =
                static_cast<float>(
                    outputHeight);

            const float baseScale =
                std::min(
                    availableWidth / visualWidth,
                    availableHeight / visualHeight);

            const float animatedScale =
                baseScale
                * transform.scale;

            const float renderedWidth =
                visualWidth
                * animatedScale;

            const float renderedHeight =
                visualHeight
                * animatedScale;

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

            renderedPose_.x = destination.x;
            renderedPose_.y = destination.y;
            renderedPose_.width = destination.w;
            renderedPose_.height = destination.h;
            renderedPose_.rotationDegrees = transform.rotationDegrees;
            renderedPose_.valid = true;

            renderedClip_ = clip;
            renderedFrameIndex_ =
                clip != nullptr
                    ? (
                        frameBlend.blend >= 0.5f
                            ? frameBlend.secondFrame
                            : frameBlend.firstFrame)
                    : 0;


            const auto renderFrame =
                [this,
                 texture,
                 &destination,
                 &transform](
                    const SDL_FRect* source)
                {
                    if (!SDL_RenderTextureRotated(
                        renderer_.get(),
                        texture,
                        source,
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
                };


            const auto setFrameAlpha =
                [this,
                 texture](
                    const Uint8 alpha) noexcept
                {
                    if (!temporalBlendAvailable_)
                    {
                        return false;
                    }

                    if (!SDL_SetTextureAlphaMod(
                        texture,
                        alpha))
                    {
                        // Alpha modulation is optional SDL renderer capability.
                        // Disable interpolation for the rest of the process and
                        // continue with ordinary source-frame rendering.
                        temporalBlendAvailable_ = false;
                        return false;
                    }

                    return true;
                };


            if (clip == nullptr)
            {
                // The still fallback is always rendered at full alpha. Ignore an
                // alpha-modulation failure because this renderer may simply not
                // support modulation at all.
                SDL_SetTextureAlphaMod(
                    texture,
                    255);

                renderFrame(
                    nullptr);
            }
            else
            {
                const SDL_FRect firstSource =
                    sourceRectForFrame(
                        *clip,
                        frameBlend.firstFrame);

                const bool canBlend =
                    temporalBlendAvailable_
                    && frameBlend.secondFrame
                        != frameBlend.firstFrame
                    && frameBlend.blend > 0.001f;

                if (!canBlend)
                {
                    SDL_SetTextureAlphaMod(
                        texture,
                        255);

                    renderFrame(
                        &firstSource);
                }
                else
                {
                    const SDL_FRect secondSource =
                        sourceRectForFrame(
                            *clip,
                            frameBlend.secondFrame);

                    const float blend =
                        std::clamp(
                            frameBlend.blend,
                            0.0f,
                            1.0f);

                    // Do NOT dim both neighboring frames at the same time.
                    //
                    // The Batch 14 implementation rendered frame A at (1-t) alpha
                    // and frame B at t alpha over a transparent window. With
                    // ordinary source-over alpha composition, two 50%-opaque
                    // copies produce only 75% final opacity even where the artwork
                    // overlaps perfectly. That made Rose's dark hat, shoes, and
                    // outfit visibly blink/translucent during every interpolation.
                    //
                    // Instead, keep one side of the transition fully opaque while
                    // the neighboring source frame fades in/out. Shared pixels
                    // therefore always have at least one full-opacity source, while
                    // differing silhouettes can still dissolve smoothly at their
                    // edges. This is not optical-flow morphing, but it removes the
                    // transparency flash without adding a video/interpolation
                    // dependency to the runtime.
                    const bool firstHalf =
                        blend < 0.5f;

                    const float halfBlend =
                        firstHalf
                            ? std::clamp(
                                blend * 2.0f,
                                0.0f,
                                1.0f)
                            : std::clamp(
                                (blend - 0.5f) * 2.0f,
                                0.0f,
                                1.0f);

                    const Uint8 firstAlpha =
                        firstHalf
                            ? static_cast<Uint8>(255)
                            : static_cast<Uint8>(
                                (1.0f - halfBlend)
                                * 255.0f);

                    const Uint8 secondAlpha =
                        firstHalf
                            ? static_cast<Uint8>(
                                halfBlend
                                * 255.0f)
                            : static_cast<Uint8>(255);

                    const bool firstAlphaApplied =
                        setFrameAlpha(
                            firstAlpha);

                    if (!firstAlphaApplied)
                    {
                        SDL_SetTextureAlphaMod(
                            texture,
                            255);

                        renderFrame(
                            &firstSource);
                    }
                    else
                    {
                        renderFrame(
                            &firstSource);

                        if (setFrameAlpha(
                            secondAlpha))
                        {
                            renderFrame(
                                &secondSource);
                        }
                        else
                        {
                            // The first source may have been drawn partially
                            // transparent in the second half. Repaint it fully opaque
                            // so renderer capability failure cannot expose the
                            // transparent desktop background for one frame.
                            SDL_SetTextureAlphaMod(
                                texture,
                                255);

                            renderFrame(
                                &firstSource);
                        }
                    }
                }

                // Alpha modulation is texture state. Best-effort restoration is
                // enough here: if unsupported, temporalBlendAvailable_ is already
                // false and future frames use hard source-frame rendering.
                SDL_SetTextureAlphaMod(
                    texture,
                    255);
            }
        }
        else
        {
            renderedPose_.valid = false;
            renderedClip_ = nullptr;
            renderedFrameIndex_ = 0;
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

    SdlAvatar::AnimationClip SdlAvatar::loadAnimationClip(
        const std::filesystem::path& path,
        const int columns,
        const int frameCount,
        const float framesPerSecond,
        const bool loop)
    {
        if (columns <= 0)
        {
            throw std::invalid_argument{
                "Rose avatar animation columns must be greater than zero."
            };
        }

        if (frameCount <= 0)
        {
            throw std::invalid_argument{
                "Rose avatar animation frame count must be greater than zero."
            };
        }

        if (framesPerSecond <= 0.0f)
        {
            throw std::invalid_argument{
                "Rose avatar animation frame rate must be greater than zero."
            };
        }

        const std::filesystem::path absolutePath =
            std::filesystem::absolute(
                path);

        if (!std::filesystem::exists(
            absolutePath))
        {
            throw std::runtime_error{
                std::string{
                    "Rose avatar animation atlas does not exist: "
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
                    "Could not load Rose avatar animation atlas '"
                }
                + pathString
                + "': "
                + SDL_GetError()
            };
        }

        const int rows =
            (frameCount + columns - 1)
            / columns;

        if (
            surface->w <= 0
            || surface->h <= 0
            || surface->w % columns != 0
            || surface->h % rows != 0)
        {
            throw std::runtime_error{
                std::string{
                    "Rose avatar animation atlas has invalid grid dimensions: "
                }
                + pathString
            };
        }

        AnimationClip clip{};
        clip.atlasWidth = surface->w;
        clip.atlasHeight = surface->h;
        clip.columns = columns;
        clip.frameCount = frameCount;
        clip.frameWidth = surface->w / columns;
        clip.frameHeight = surface->h / rows;
        clip.framesPerSecond = framesPerSecond;
        clip.loop = loop;

        clip.alpha.resize(
            static_cast<std::size_t>(
                clip.atlasWidth
                * clip.atlasHeight));

        for (int y = 0;
            y < clip.atlasHeight;
            ++y)
        {
            for (int x = 0;
                x < clip.atlasWidth;
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
                            "Could not read Rose animation atlas alpha: "
                        }
                        + SDL_GetError()
                    };
                }

                const std::size_t index =
                    static_cast<std::size_t>(
                        y
                        * clip.atlasWidth
                        + x);

                clip.alpha[index] = alpha;
            }
        }

        clip.texture.reset(
            SDL_CreateTextureFromSurface(
                renderer_.get(),
                surface.get()));

        if (!clip.texture)
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose animation texture: "
                }
                + SDL_GetError()
            };
        }

        return clip;
    }


    void SdlAvatar::loadAnimationAtlas(
        const AvatarState state,
        const std::filesystem::path& path,
        const int columns,
        const int frameCount,
        const float framesPerSecond,
        const bool loop)
    {
        const std::size_t slot =
            animationSlot(
                state);

        if (slot >= animations_.size())
        {
            throw std::invalid_argument{
                "Rose avatar animation state is outside the configured slot range."
            };
        }

        // Replace the slot only after the entire new clip has loaded. If decoding,
        // alpha extraction, or GPU texture creation fails, the old clip remains
        // usable and the avatar can continue rendering.
        AnimationClip clip =
            loadAnimationClip(
                path,
                columns,
                frameCount,
                framesPerSecond,
                loop);

        animations_[slot] =
            std::move(
                clip);
    }


    void SdlAvatar::loadPostureTransitionAtlas(
        const std::filesystem::path& path,
        const int columns,
        const int frameCount)
    {
        // Transition playback is driven by a normalized posture progress value, not
        // by this clip's source FPS. A nominal positive FPS keeps AnimationClip's
        // invariant simple while the renderer maps 0..1 directly across frames.
        AnimationClip clip =
            loadAnimationClip(
                path,
                columns,
                frameCount,
                1.0f,
                false);

        postureTransition_ =
            std::move(
                clip);
    }


    void SdlAvatar::loadDefaultAnimations(
        const std::filesystem::path& directory)
    {
        // Asset absence is intentionally non-fatal. Rose can still run with the
        // static RoseIdle.png fallback when a user is iterating on animation files.
        const auto loadIfPresent =
            [this, &directory](
                const AvatarState state,
                const char* fileName,
                const int columns,
                const int frameCount,
                const float framesPerSecond,
                const bool loop)
            {
                const std::filesystem::path path =
                    directory
                    / fileName;

                if (!std::filesystem::exists(
                    path))
                {
                    return;
                }

                loadAnimationAtlas(
                    state,
                    path,
                    columns,
                    frameCount,
                    framesPerSecond,
                    loop);
            };

        // Batch 14 intentionally plays the 7-8 FPS authoring clips much more
        // slowly. Combined with 60 Hz temporal blending, the source art reads as
        // deliberate character motion instead of a rapid slideshow.
        loadIfPresent(
            AvatarState::Idle,
            "RoseIdleBook.atlas.png",
            4,
            8,
            2.0f,
            true);

        loadIfPresent(
            AvatarState::Listening,
            "RoseListening.atlas.png",
            5,
            10,
            3.25f,
            true);

        loadIfPresent(
            AvatarState::Thinking,
            "RoseThinking.atlas.png",
            5,
            10,
            3.0f,
            true);

        loadIfPresent(
            AvatarState::Speaking,
            "RoseTalking.atlas.png",
            5,
            10,
            3.5f,
            true);

        loadIfPresent(
            AvatarState::Working,
            "RoseWorking.atlas.png",
            5,
            10,
            3.0f,
            true);

        loadIfPresent(
            AvatarState::Notification,
            "RoseNotification.atlas.png",
            5,
            10,
            3.0f,
            false);

        const std::filesystem::path standupPath =
            directory
            / "RoseStandup.atlas.png";

        if (std::filesystem::exists(
            standupPath))
        {
            loadPostureTransitionAtlas(
                standupPath,
                5,
                10);
        }

        // There is no dedicated Confused/Error authoring clip yet, so that state
        // deliberately remains seated and falls back to the still sprite plus its
        // subtle wobble transform.
    }


    const SdlAvatar::AnimationClip* SdlAvatar::animationFor(
        const AvatarState state) const noexcept
    {
        const std::size_t slot =
            animationSlot(
                state);

        if (
            slot >= animations_.size()
            || !animations_[slot].has_value())
        {
            return nullptr;
        }

        return &animations_[slot].value();
    }


    SdlAvatar::FrameBlend SdlAvatar::frameBlendForElapsed(
        const AnimationClip& clip,
        const float elapsedSeconds) noexcept
    {
        if (
            clip.frameCount <= 1
            || clip.framesPerSecond <= 0.0f)
        {
            return FrameBlend{};
        }

        const float rawPosition =
            std::max(
                0.0f,
                elapsedSeconds)
            * clip.framesPerSecond;

        float position =
            rawPosition;

        if (clip.loop)
        {
            position =
                std::fmod(
                    rawPosition,
                    static_cast<float>(
                        clip.frameCount));
        }
        else
        {
            position =
                std::min(
                    rawPosition,
                    static_cast<float>(
                        clip.frameCount - 1));
        }

        const int firstFrame =
            std::clamp(
                static_cast<int>(
                    std::floor(
                        position)),
                0,
                clip.frameCount - 1);

        int secondFrame =
            firstFrame;

        if (clip.loop)
        {
            secondFrame =
                (firstFrame + 1)
                % clip.frameCount;
        }
        else if (
            firstFrame
            < clip.frameCount - 1)
        {
            secondFrame =
                firstFrame + 1;
        }

        const float fractional =
            std::clamp(
                position
                - std::floor(
                    position),
                0.0f,
                1.0f);

        // Hold the source pose for roughly the first third of each interval, then
        // ease the next frame in. This avoids a constant ghosted/double-image look
        // while removing the abrupt 7-8 FPS frame jumps that made Batch 13 feel
        // unnaturally fast.
        constexpr float blendStart{
            0.35f
        };

        const float normalizedBlend =
            std::clamp(
                (fractional - blendStart)
                / (1.0f - blendStart),
                0.0f,
                1.0f);

        const float easedBlend =
            normalizedBlend
            * normalizedBlend
            * (3.0f - 2.0f * normalizedBlend);

        return FrameBlend{
            .firstFrame = firstFrame,
            .secondFrame = secondFrame,
            .blend = easedBlend
        };
    }


    SdlAvatar::FrameBlend SdlAvatar::frameBlendForProgress(
        const AnimationClip& clip,
        const float normalizedProgress) noexcept
    {
        if (clip.frameCount <= 1)
        {
            return FrameBlend{};
        }

        // Posture progress is monotonic 0..1 while standing and monotonic 1..0
        // while sitting. Mapping the progress directly to atlas position therefore
        // plays RoseStandup forward or backward automatically, including seamless
        // mid-transition reversals.
        const float position =
            std::clamp(
                normalizedProgress,
                0.0f,
                1.0f)
            * static_cast<float>(
                clip.frameCount - 1);

        const int firstFrame =
            std::clamp(
                static_cast<int>(
                    std::floor(
                        position)),
                0,
                clip.frameCount - 1);

        const int secondFrame =
            std::min(
                firstFrame + 1,
                clip.frameCount - 1);

        const float fractional =
            std::clamp(
                position
                - static_cast<float>(
                    firstFrame),
                0.0f,
                1.0f);

        const float easedBlend =
            fractional
            * fractional
            * (3.0f - 2.0f * fractional);

        return FrameBlend{
            .firstFrame = firstFrame,
            .secondFrame = secondFrame,
            .blend = easedBlend
        };
    }


    SDL_FRect SdlAvatar::sourceRectForFrame(
        const AnimationClip& clip,
        const int frameIndex) noexcept
    {
        const int safeFrameIndex =
            std::clamp(
                frameIndex,
                0,
                std::max(
                    0,
                    clip.frameCount - 1));

        const int column =
            safeFrameIndex
            % clip.columns;

        const int row =
            safeFrameIndex
            / clip.columns;

        return SDL_FRect{
            static_cast<float>(
                column
                * clip.frameWidth),

            static_cast<float>(
                row
                * clip.frameHeight),

            static_cast<float>(
                clip.frameWidth),

            static_cast<float>(
                clip.frameHeight)
        };
    }


    bool SdlAvatar::isSpritePixelAt(
        const float windowX,
        const float windowY) const noexcept
    {
        if (!renderedPose_.valid)
        {
            return false;
        }

        const int sourcePixelWidth =
            renderedClip_ != nullptr
                ? renderedClip_->frameWidth
                : spritePixelWidth_;

        const int sourcePixelHeight =
            renderedClip_ != nullptr
                ? renderedClip_->frameHeight
                : spritePixelHeight_;

        if (
            sourcePixelWidth <= 0
            || sourcePixelHeight <= 0)
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

        const float x =
            windowX
            - centerX;

        const float y =
            windowY
            - centerY;

        // Undo the rendered rotation. Rendering rotates clockwise, so hit testing
        // rotates the mouse point in the opposite direction.
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

        const int sourceX =
            static_cast<int>(
                destinationX
                / renderedPose_.width
                * static_cast<float>(
                    sourcePixelWidth));

        const int sourceY =
            static_cast<int>(
                destinationY
                / renderedPose_.height
                * static_cast<float>(
                    sourcePixelHeight));

        if (
            sourceX < 0
            || sourceY < 0
            || sourceX >= sourcePixelWidth
            || sourceY >= sourcePixelHeight)
        {
            return false;
        }

        std::uint8_t alpha{ 0 };

        if (renderedClip_ != nullptr)
        {
            const int frameColumn =
                renderedFrameIndex_
                % renderedClip_->columns;

            const int frameRow =
                renderedFrameIndex_
                / renderedClip_->columns;

            const int atlasX =
                frameColumn
                * renderedClip_->frameWidth
                + sourceX;

            const int atlasY =
                frameRow
                * renderedClip_->frameHeight
                + sourceY;

            const std::size_t index =
                static_cast<std::size_t>(
                    atlasY
                    * renderedClip_->atlasWidth
                    + atlasX);

            if (index >= renderedClip_->alpha.size())
            {
                return false;
            }

            alpha =
                renderedClip_->alpha[index];
        }
        else
        {
            if (spriteAlpha_.empty())
            {
                return false;
            }

            const std::size_t index =
                static_cast<std::size_t>(
                    sourceY
                    * spritePixelWidth_
                    + sourceX);

            if (index >= spriteAlpha_.size())
            {
                return false;
            }

            alpha =
                spriteAlpha_[index];
        }

        // Ignore almost-transparent antialiasing fringe pixels.
        constexpr std::uint8_t interactionAlphaThreshold{
            16
        };

        return
            alpha
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
                    * 0.8f);


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
                    * 1.4f);


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
                    * 0.75f)
                * 2.5f;


            transform.offsetY =
                std::sin(
                    elapsedSeconds
                    * 1.05f)
                * 3.0f;


            transform.rotationDegrees =
                static_cast<double>(
                    std::sin(
                        elapsedSeconds
                        * 0.65f)
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
                    * 2.0f);


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
                    * 1.15f)
                * 1.5f;


            transform.offsetY =
                std::sin(
                    elapsedSeconds
                    * 1.55f)
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
                        * 2.5f));


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
                        * 2.0f)
                    * 3.0f);

            break;
        }
        }


        return transform;
    }

} // namespace rose::avatar