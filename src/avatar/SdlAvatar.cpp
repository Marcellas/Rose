#include "avatar/SdlAvatar.h"

#include <SDL3/SDL.h>

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


        SdlRuntime& sdlRuntime()
        {
            static SdlRuntime runtime;

            return runtime;
        }

    } // namespace


    SdlAvatar::SdlAvatar(
        const int width,
        const int height)
    {
        if (width <= 0 || height <= 0)
        {
            throw std::invalid_argument{
                "SdlAvatar dimensions must be greater than zero."
            };
        }


        // Ensure process-level SDL initialization happens before creating the
        // window or renderer.
        (void)sdlRuntime();


        SDL_Window* rawWindow{ nullptr };
        SDL_Renderer* rawRenderer{ nullptr };


        const SDL_WindowFlags flags =
            SDL_WINDOW_BORDERLESS
            | SDL_WINDOW_ALWAYS_ON_TOP;


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


    bool SdlAvatar::processEvents()
    {
        SDL_Event event{};


        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                return false;


            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    dragging_ = true;

                    dragStartMouseX_ =
                        event.button.x;

                    dragStartMouseY_ =
                        event.button.y;


                    SDL_GetWindowPosition(
                        window_.get(),
                        &dragStartWindowX_,
                        &dragStartWindowY_);
                }

                break;


            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    dragging_ = false;
                }

                break;


            case SDL_EVENT_MOUSE_MOTION:
                if (dragging_)
                {
                    const int newX =
                        dragStartWindowX_
                        + static_cast<int>(
                            event.motion.x
                            - dragStartMouseX_);


                    const int newY =
                        dragStartWindowY_
                        + static_cast<int>(
                            event.motion.y
                            - dragStartMouseY_);


                    SDL_SetWindowPosition(
                        window_.get(),
                        newX,
                        newY);
                }

                break;
            }
        }


        return true;
    }


    void SdlAvatar::render()
    {
        const AvatarState state =
            state_.load(
                std::memory_order_relaxed);


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


        SDL_RenderClear(
            renderer_.get());


        // Draw a simple central "avatar body" placeholder.
        //
        // This proves actual geometry rendering before we introduce textures,
        // PNG loading, sprite atlases, animation timers, and alpha compositing.
        SDL_FRect body{
            85.0f,
            40.0f,
            150.0f,
            220.0f
        };


        SDL_SetRenderDrawColor(
            renderer_.get(),
            230,
            225,
            240,
            255);


        SDL_RenderFillRect(
            renderer_.get(),
            &body);


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

} // namespace rose::avatar