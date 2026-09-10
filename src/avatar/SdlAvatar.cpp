#include "avatar/SdlAvatar.h"

#include "platform/SdlRuntime.h"
#include <SDL3_image/SDL_image.h>

#include <filesystem>
#include <stdexcept>
#include <string>
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
            // Ignore mouse events belonging to another SDL window.
            if (event.button.windowID != windowId)
            {
                break;
            }


            if (event.button.button == SDL_BUTTON_LEFT)
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
            const float scale =
                std::min(
                    availableWidth / spriteWidth_,
                    availableHeight / spriteHeight_);


            const float renderedWidth =
                spriteWidth_
                * scale;


            const float renderedHeight =
                spriteHeight_
                * scale;


            // Center Rose inside the current avatar window.
            const SDL_FRect destination{
                (availableWidth - renderedWidth)
                    * 0.5f,

                (availableHeight - renderedHeight)
                    * 0.5f,

                renderedWidth,

                renderedHeight
            };


            if (!SDL_RenderTexture(
                renderer_.get(),
                spriteTexture_.get(),
                nullptr,
                &destination))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not render Rose avatar texture: "
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


        TexturePtr newTexture{
            IMG_LoadTexture(
                renderer_.get(),
                pathString.c_str())
        };


        if (!newTexture)
        {
            throw std::runtime_error{
                std::string{
                    "Could not load Rose avatar image '"
                }
                + pathString
                + "': "
                + SDL_GetError()
            };
        }


        float width{ 0.0f };
        float height{ 0.0f };


        if (!SDL_GetTextureSize(
            newTexture.get(),
            &width,
            &height))
        {
            throw std::runtime_error{
                std::string{
                    "Could not query Rose avatar texture size: "
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
            width;

        spriteHeight_ =
            height;
    }

} // namespace rose::avatar