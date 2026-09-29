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
                ? clip->frameTextures.front().get()
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

            // Give rotation, bounce, and an expanding posture room inside the
            // actual window rectangle. A shaped window cannot draw beyond it.
            constexpr float motionInset = 40.0f;
            const float baseScale =
                std::min(
                    std::max(1.0f, availableWidth - motionInset * 2.0f) / visualWidth,
                    std::max(1.0f, availableHeight - motionInset * 2.0f) / visualHeight);

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

            updateWindowShape(
                clip,
                renderedFrameIndex_,
                renderedFrameIndex_,
                outputWidth,
                outputHeight);


            // Cross-fading two separate silhouettes produces a second ghost pose
            // (including authoring artifacts already baked into either source).
            // Select one frame and let the presentation timeline control timing.
            SDL_Texture* frameTexture = clip != nullptr
                ? clip->frameTextures[static_cast<std::size_t>(renderedFrameIndex_)].get()
                : texture;
            if (!SDL_RenderTextureRotated(
                renderer_.get(), frameTexture, nullptr, &destination,
                transform.rotationDegrees, nullptr, SDL_FLIP_NONE))
            {
                throw std::runtime_error{
                    std::string{ "Could not render animated Rose avatar: " }
                    + SDL_GetError()
                };
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
        shapeFirstFrame_ = -1;
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

        if (!SDL_SetSurfaceBlendMode(surface.get(), SDL_BLENDMODE_NONE))
        {
            throw std::runtime_error{
                std::string{ "Could not copy Rose atlas alpha: " } + SDL_GetError()
            };
        }
        clip.frameTextures.reserve(static_cast<std::size_t>(frameCount));
        for (int frame = 0; frame < frameCount; ++frame)
        {
            SurfacePtr isolated{
                SDL_CreateSurface(clip.frameWidth, clip.frameHeight,
                    SDL_PIXELFORMAT_RGBA32)
            };
            const SDL_Rect source{
                (frame % columns) * clip.frameWidth,
                (frame / columns) * clip.frameHeight,
                clip.frameWidth, clip.frameHeight
            };
            if (!isolated || !SDL_BlitSurface(surface.get(), &source,
                isolated.get(), nullptr))
            {
                throw std::runtime_error{
                    std::string{ "Could not isolate Rose animation frame: " }
                    + SDL_GetError()
                };
            }
            TexturePtr frameTexture{
                SDL_CreateTextureFromSurface(renderer_.get(), isolated.get())
            };
            if (!frameTexture)
            {
                throw std::runtime_error{
                    std::string{ "Could not create Rose animation frame texture: " }
                    + SDL_GetError()
                };
            }
            clip.frameTextures.push_back(std::move(frameTexture));
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

        return FrameBlend{
            .firstFrame = firstFrame,
            .secondFrame = secondFrame,
            .blend = fractional
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


    void SdlAvatar::updateWindowShape(
        const AnimationClip* clip,
        const int firstFrame,
        const int secondFrame,
        const int outputWidth,
        const int outputHeight)
    {
        int width{ 0 };
        int height{ 0 };
        if (!SDL_GetWindowSize(window_.get(), &width, &height)
            || width <= 0 || height <= 0)
        {
            throw std::runtime_error{
                std::string{ "Could not query Rose window shape size: " }
                + SDL_GetError()
            };
        }

        if (clip == shapeClip_
            && firstFrame == shapeFirstFrame_
            && secondFrame == shapeSecondFrame_
            && width == shapeWidth_
            && height == shapeHeight_
            && outputWidth == shapeOutputWidth_
            && outputHeight == shapeOutputHeight_)
        {
            return;
        }

        SurfacePtr shape{
            SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32)
        };

        if (!shape)
        {
            throw std::runtime_error{
                std::string{ "Could not create Rose animation window shape: " }
                + SDL_GetError()
            };
        }

        // Count occupied pixels in a summed-area table. The two source frames
        // both contribute because they may be cross-faded on this presentation
        // interval. A margin accommodates Rose's subtle UI transforms without
        // rebuilding a Windows region on every 60 Hz render.
        const int stride = width + 1;
        std::vector<std::uint32_t> occupied(
            static_cast<std::size_t>(stride)
            * static_cast<std::size_t>(height + 1), 0);

        constexpr double pi{ 3.14159265358979323846 };
        const double radians = -renderedPose_.rotationDegrees * pi / 180.0;
        const float cosine = static_cast<float>(std::cos(radians));
        const float sine = static_cast<float>(std::sin(radians));
        const float centerX = renderedPose_.x + renderedPose_.width * 0.5f;
        const float centerY = renderedPose_.y + renderedPose_.height * 0.5f;
        const int sourceWidth = clip ? clip->frameWidth : spritePixelWidth_;
        const int sourceHeight = clip ? clip->frameHeight : spritePixelHeight_;

        const auto alphaAt = [&](const int frame, const int x, const int y)
        {
            if (clip)
            {
                const int column = frame % clip->columns;
                const int row = frame / clip->columns;
                const auto index = static_cast<std::size_t>(
                    (row * clip->frameHeight + y) * clip->atlasWidth
                    + column * clip->frameWidth + x);
                return clip->alpha[index];
            }

            return spriteAlpha_[static_cast<std::size_t>(
                y * spritePixelWidth_ + x)];
        };

        for (int y = 0; y < height; ++y)
        {
            std::uint32_t rowSum{ 0 };
            for (int x = 0; x < width; ++x)
            {
                const float relativeX =
                    (static_cast<float>(x) + 0.5f) * outputWidth / width - centerX;
                const float relativeY =
                    (static_cast<float>(y) + 0.5f) * outputHeight / height - centerY;
                const float localX = relativeX * cosine - relativeY * sine
                    + renderedPose_.width * 0.5f;
                const float localY = relativeX * sine + relativeY * cosine
                    + renderedPose_.height * 0.5f;

                if (localX >= 0.0f && localY >= 0.0f
                    && localX < renderedPose_.width
                    && localY < renderedPose_.height)
                {
                    const int sx = std::min(sourceWidth - 1,
                        static_cast<int>(localX / renderedPose_.width * sourceWidth));
                    const int sy = std::min(sourceHeight - 1,
                        static_cast<int>(localY / renderedPose_.height * sourceHeight));
                    rowSum += alphaAt(firstFrame, sx, sy) > 0
                        || alphaAt(secondFrame, sx, sy) > 0;
                }

                occupied[static_cast<std::size_t>(y + 1) * stride + x + 1] =
                    occupied[static_cast<std::size_t>(y) * stride + x + 1]
                    + rowSum;
            }
        }

        constexpr int motionMargin{ 18 };
        for (int y = 0; y < height; ++y)
        {
            const int top = std::max(0, y - motionMargin);
            const int bottom = std::min(height, y + motionMargin + 1);
            for (int x = 0; x < width; ++x)
            {
                const int left = std::max(0, x - motionMargin);
                const int right = std::min(width, x + motionMargin + 1);
                const auto count =
                    occupied[static_cast<std::size_t>(bottom) * stride + right]
                    - occupied[static_cast<std::size_t>(top) * stride + right]
                    - occupied[static_cast<std::size_t>(bottom) * stride + left]
                    + occupied[static_cast<std::size_t>(top) * stride + left];

                if (!SDL_WriteSurfacePixel(shape.get(), x, y,
                    255, 255, 255, count > 0 ? 255 : 0))
                {
                    throw std::runtime_error{
                        std::string{ "Could not write Rose window shape: " }
                        + SDL_GetError()
                    };
                }
            }
        }

        if (!SDL_SetWindowShape(window_.get(), shape.get()))
        {
            throw std::runtime_error{
                std::string{ "Could not update Rose animation window shape: " }
                + SDL_GetError()
            };
        }

        shapeClip_ = clip;
        shapeFirstFrame_ = firstFrame;
        shapeSecondFrame_ = secondFrame;
        shapeWidth_ = width;
        shapeHeight_ = height;
        shapeOutputWidth_ = outputWidth;
        shapeOutputHeight_ = outputHeight;
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
