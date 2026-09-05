#pragma once

#include "avatar/IAvatar.h"

#include <atomic>
#include <memory>


struct SDL_Window;
struct SDL_Renderer;


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
            int width,
            int height);

        ~SdlAvatar() override;


        SdlAvatar(const SdlAvatar&) = delete;
        SdlAvatar& operator=(const SdlAvatar&) = delete;

        SdlAvatar(SdlAvatar&&) = delete;
        SdlAvatar& operator=(SdlAvatar&&) = delete;


        void setState(
            AvatarState state) override;


        // Returns false when the user closes the avatar window.
        [[nodiscard]]
        bool processEvents();


        void render();


    private:
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

        WindowPtr window_;

        RendererPtr renderer_;

    private:
    bool dragging_{ false };

    float dragStartMouseX_{ 0.0f };
    float dragStartMouseY_{ 0.0f };

    int dragStartWindowX_{ 0 };
    int dragStartWindowY_{ 0 };
    };

} // namespace rose::avatar