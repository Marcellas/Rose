#pragma once

#include "avatar/AvatarInteraction.h"
#include "avatar/AvatarPresentationTimeline.h"
#include "avatar/IAvatar.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
struct SDL_Texture;
struct SDL_FRect;

namespace rose::platform
{
    class SdlRuntime;
}

namespace rose::avatar
{

    // -----------------------------------------------------------------------------
    // SdlAvatar
    // -----------------------------------------------------------------------------
    //
    // SDL presentation for Rose's desktop character.
    //
    // RoseCore still publishes only semantic AvatarState values. This class owns
    // the GPU resources and decides which visual clip corresponds to each state.
    // The model therefore never depends on sprite sheets, frame rates, or SDL.
    //
    // THREADING
    // ---------
    // setState() is worker-safe and performs only an atomic write. SDL window,
    // texture, event, and render operations remain on the main/UI thread.
    class SdlAvatar final
        : public IAvatar
    {
    public:
        SdlAvatar(
            platform::SdlRuntime& runtime,
            int width,
            int height);

        ~SdlAvatar() override;

        SdlAvatar(const SdlAvatar&) = delete;
        SdlAvatar& operator=(const SdlAvatar&) = delete;
        SdlAvatar(SdlAvatar&&) = delete;
        SdlAvatar& operator=(SdlAvatar&&) = delete;

        void setState(
            AvatarState state) override;

        [[nodiscard]]
        bool handleEvent(
            const SDL_Event& event);

        void render();

        // UI-thread-only temporary visual override. Semantic state publication
        // continues underneath and resumes automatically when the override clears.
        void setPresentationOverride(
            std::optional<AvatarState> state) noexcept;

        [[nodiscard]]
        std::vector<AvatarInteractionEvent> takeInteractions();

        // Fallback still image. It is also used to establish the window shape so
        // Rose remains draggable/click-through even if every animation asset is
        // missing.
        void loadSprite(
            const std::filesystem::path& path);

        // Loads one pre-baked PNG atlas for a semantic avatar state.
        //
        // Runtime deliberately uses PNG atlases instead of decoding WebM. The
        // original WebM files remain authoring assets, while Rose's runtime keeps
        // one small, deterministic SDL_image dependency and no video-decoder
        // thread/process. This is especially valuable for the offline-first MVP.
        void loadAnimationAtlas(
            AvatarState state,
            const std::filesystem::path& path,
            int columns,
            int frameCount,
            float framesPerSecond,
            bool loop = true);

        // Loads the physical seated <-> standing transition. The same source clip
        // is played forward when Rose stands and backwards when she sits. Keeping
        // this separate from AvatarState prevents posture artwork from becoming a
        // fake semantic state.
        void loadPostureTransitionAtlas(
            const std::filesystem::path& path,
            int columns,
            int frameCount);

        // Loads the standard Rose animation set from an asset directory. Missing
        // atlases are skipped independently and fall back to RoseIdle.png.
        void loadDefaultAnimations(
            const std::filesystem::path& directory);

    private:
        struct RenderedSpritePose
        {
            float x{ 0.0f };
            float y{ 0.0f };
            float width{ 0.0f };
            float height{ 0.0f };
            double rotationDegrees{ 0.0 };
            bool valid{ false };
        };

        struct AvatarTransform
        {
            float offsetX{ 0.0f };
            float offsetY{ 0.0f };
            float scale{ 1.0f };
            double rotationDegrees{ 0.0 };
        };

        struct TextureDeleter
        {
            void operator()(SDL_Texture* texture) const noexcept;
        };

        using TexturePtr =
            std::unique_ptr<SDL_Texture, TextureDeleter>;

        struct WindowDeleter
        {
            void operator()(SDL_Window* window) const noexcept;
        };

        struct RendererDeleter
        {
            void operator()(SDL_Renderer* renderer) const noexcept;
        };

        using WindowPtr =
            std::unique_ptr<SDL_Window, WindowDeleter>;

        using RendererPtr =
            std::unique_ptr<SDL_Renderer, RendererDeleter>;

        struct AnimationClip
        {
            TexturePtr texture;
            std::vector<std::uint8_t> alpha;

            int atlasWidth{ 0 };
            int atlasHeight{ 0 };
            int frameWidth{ 0 };
            int frameHeight{ 0 };
            int columns{ 1 };
            int frameCount{ 1 };

            float framesPerSecond{ 1.0f };
            bool loop{ true };
        };

        // Two neighboring source frames plus a temporal blend amount. Rose's
        // authoring clips are only 7-8 FPS; blending at the 60 Hz presentation
        // rate creates perceptual in-between frames without multiplying atlas
        // memory or adding a video decoder.
        struct FrameBlend
        {
            int firstFrame{ 0 };
            int secondFrame{ 0 };
            float blend{ 0.0f };
        };

        static constexpr std::size_t animationSlotCount{ 7 };

        [[nodiscard]]
        static constexpr std::size_t animationSlot(
            AvatarState state) noexcept
        {
            return static_cast<std::size_t>(state);
        }

        [[nodiscard]]
        const AnimationClip* animationFor(
            AvatarState state) const noexcept;

        [[nodiscard]]
        AnimationClip loadAnimationClip(
            const std::filesystem::path& path,
            int columns,
            int frameCount,
            float framesPerSecond,
            bool loop);

        [[nodiscard]]
        static FrameBlend frameBlendForElapsed(
            const AnimationClip& clip,
            float elapsedSeconds) noexcept;

        [[nodiscard]]
        static FrameBlend frameBlendForProgress(
            const AnimationClip& clip,
            float normalizedProgress) noexcept;

        [[nodiscard]]
        static SDL_FRect sourceRectForFrame(
            const AnimationClip& clip,
            int frameIndex) noexcept;

        [[nodiscard]]
        AvatarTransform calculateTransform(
            AvatarState state,
            float elapsedSeconds) const;

        [[nodiscard]]
        bool isSpritePixelAt(
            float windowX,
            float windowY) const noexcept;

        // Latest semantic activity requested by the worker thread. The UI thread
        // consumes this through presentationTimeline_, which is free to finish
        // physical sit/stand motion before visually settling on the request.
        std::atomic<AvatarState> requestedState_{ AvatarState::Idle };

        // Presentation-only override used by direct desktop interaction and the
        // animation demo. Main/UI thread owns this optional value.
        std::optional<AvatarState> presentationOverride_;

        // UI-thread-only interaction queue. It intentionally carries high-level
        // gestures rather than SDL events so the rest of Rose never depends on SDL.
        std::vector<AvatarInteractionEvent> pendingInteractions_;

        // UI-thread-only visual state machine. It deliberately owns no SDL
        // resources, so the transition policy can be tested independently.
        AvatarPresentationTimeline presentationTimeline_;

        // Borrowed process-level SDL runtime. main() owns it and guarantees it
        // outlives this avatar.
        platform::SdlRuntime& runtime_;

        WindowPtr window_;
        RendererPtr renderer_;

        // Fallback still visual.
        TexturePtr spriteTexture_;
        std::vector<std::uint8_t> spriteAlpha_;
        int spritePixelWidth_{ 0 };
        int spritePixelHeight_{ 0 };
        float spriteWidth_{ 0.0f };
        float spriteHeight_{ 0.0f };

        // One independently owned GPU atlas per presentation state. A missing slot
        // simply uses the fallback sprite.
        std::array<std::optional<AnimationClip>, animationSlotCount>
            animations_{};

        // Dedicated posture transition source. RoseStandup is rendered forward for
        // sit->stand and backwards for stand->sit. The transition duration is
        // intentionally slower than the original 7 FPS authoring clip.
        std::optional<AnimationClip> postureTransition_;
        float postureTransitionSeconds_{ 2.6f };

        RenderedSpritePose renderedPose_;

        // Hit testing uses the exact source frame that was most recently rendered.
        // This pointer is non-owning and remains valid because animation clips are
        // loaded during startup and not replaced during normal rendering.
        const AnimationClip* renderedClip_{ nullptr };
        int renderedFrameIndex_{ 0 };

        // Temporal cross-fading is optional presentation polish. If the active SDL
        // renderer does not support texture alpha modulation, disable blending and
        // continue with the slower source-frame timing rather than failing Rose.
        bool temporalBlendAvailable_{ true };

        // Window dragging uses global desktop coordinates so moving the window does
        // not change the coordinate system being measured.
        bool pointerArmed_{ false };
        bool dragging_{ false };
        float dragStartGlobalMouseX_{ 0.0f };
        float dragStartGlobalMouseY_{ 0.0f };
        int dragStartWindowX_{ 0 };
        int dragStartWindowY_{ 0 };

        static constexpr float dragThresholdPixels_{ 6.0f };
    };

} // namespace rose::avatar
