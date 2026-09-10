#pragma once

#include "avatar/IAvatar.h"

#include <atomic>
#include <memory>
#include <filesystem>
#include <chrono>
#include <cstdint>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
struct SDL_Texture;

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
    // First graphical implementation of IAvatar.
    //
    // RESPONSIBILITY
    // --------------
    // SdlAvatar owns:
    //
    //     SDL window
    //     SDL renderer
    //
    // It receives logical AvatarState updates through IAvatar::setState() and
    // renders a simple placeholder representation for that state.
    //
    // IMPORTANT THREADING RULE
    // ------------------------
    //
    // setState() is intentionally safe to call from a future inference worker.
    //
    // It does NOT call SDL.
    //
    // SDL window/event/render operations remain on the main thread:
    //
    //     setState()          worker-safe atomic write
    //     processEvents()     main thread only
    //     render()            main thread only
    //
    // This separation is critical once Rose's model inference moves off the UI
    // thread.
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


    // Handle one event supplied by the application's central SDL event pump.
    //
    // Returns false when this avatar window has been asked to close.
    //
    // SdlAvatar does NOT call SDL_PollEvent() itself.
    [[nodiscard]]
    bool handleEvent(
        const SDL_Event& event);


    void render();

    // Load the visual asset used by the desktop avatar.
    //
    // SdlAvatar owns the resulting GPU texture. The caller only supplies the
    // filesystem location.
    //
    // This keeps asset selection outside SdlAvatar while keeping graphics-resource
    // ownership inside the presentation object.
    void loadSprite(
        const std::filesystem::path& path);

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

    // Calculates a lightweight presentation transform for the current state.
    //
    // This is pure presentation logic. RoseCore still only reports semantic
    // activity states and knows nothing about animation.
    [[nodiscard]]
    AvatarTransform calculateTransform(
        AvatarState state,
        float elapsedSeconds) const;

    struct TextureDeleter
    {
        void operator()(
            SDL_Texture* texture) const noexcept;
    };

    using TexturePtr =
        std::unique_ptr<
        SDL_Texture,
        TextureDeleter>;

    struct WindowDeleter
    {
        void operator()(
            SDL_Window* window) const noexcept;
    };


    struct RendererDeleter
    {
        void operator()(
            SDL_Renderer* renderer) const noexcept;
    };


    using WindowPtr =
        std::unique_ptr<
        SDL_Window,
        WindowDeleter>;

    using RendererPtr =
        std::unique_ptr<
        SDL_Renderer,
        RendererDeleter>;


    std::atomic<AvatarState> state_{
        AvatarState::Idle
    };

    // Borrowed process-level SDL runtime.
    //
    // main() owns it and guarantees it outlives this avatar.
    platform::SdlRuntime& runtime_;

    WindowPtr window_;

    RendererPtr renderer_;

private:
    // One byte of alpha per original sprite pixel.
    //
    // This stays on the CPU so mouse hit-testing does not require reading texture
    // data back from the GPU.
    std::vector<std::uint8_t> spriteAlpha_;

    int spritePixelWidth_{ 0 };
    int spritePixelHeight_{ 0 };


    // Last pose actually submitted to the renderer.
    //
    // Mouse interaction occurs on the same SDL/main thread, so this does not
    // require synchronization.
    RenderedSpritePose renderedPose_;

    [[nodiscard]]
    bool isSpritePixelAt(
        float windowX,
        float windowY) const noexcept;

    // Animation timing is owned entirely by the SDL/main thread.
    //
    // setState() may be called from Rose's worker, but render() detects the state
    // transition and resets the animation epoch safely on the UI thread.
    AvatarState lastRenderedState_{
        AvatarState::Idle
    };


    std::chrono::steady_clock::time_point animationStart_{
        std::chrono::steady_clock::now()
    };
    // -----------------------------------------------------------------------------
    // Window dragging
    // -----------------------------------------------------------------------------
    //
    // Mouse coordinates used for dragging are DESKTOP coordinates.
    //
    // Using window-relative mouse coordinates here creates a feedback loop:
    // moving the window changes the coordinate system that the mouse position is
    // measured against.
    //
    // Global coordinates remain stable while the window itself moves.
    bool dragging_{ false };

    float dragStartGlobalMouseX_{ 0.0f };
    float dragStartGlobalMouseY_{ 0.0f };

    int dragStartWindowX_{ 0 };
    int dragStartWindowY_{ 0 };

    // GPU-owned visual representation of Rose.
    //
    // Created and destroyed on the SDL/main thread.
    TexturePtr spriteTexture_;

    float spriteWidth_{ 0.0f };
    float spriteHeight_{ 0.0f };
};
} // namespace rose::avatar