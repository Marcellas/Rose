#include "platform/SdlRuntime.h"
#include "ui/MathRenderer.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>


int main()
{
    try
    {
        rose::platform::SdlRuntime sdlRuntime;


        SDL_Window* window{
            nullptr
        };

        SDL_Renderer* renderer{
            nullptr
        };


        if (!SDL_CreateWindowAndRenderer(
            "Rose Math Sandbox",
            1100,
            420,
            SDL_WINDOW_RESIZABLE,
            &window,
            &renderer))
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose math sandbox: "
                }
                + SDL_GetError()
            };
        }


        struct WindowGuard
        {
            SDL_Window* value;

            ~WindowGuard()
            {
                if (value != nullptr)
                {
                    SDL_DestroyWindow(
                        value);
                }
            }
        } windowGuard{
            window
        };


        struct RendererGuard
        {
            SDL_Renderer* value;

            ~RendererGuard()
            {
                if (value != nullptr)
                {
                    SDL_DestroyRenderer(
                        value);
                }
            }
        } rendererGuard{
            renderer
        };


        // The dependency stays isolated behind MathRenderer. For the MVP sandbox
        // we point directly at the checked-out MicroTeX resources. Packaging can
        // later copy them beside Rose.exe and resolve them from the executable.
        rose::ui::MathRenderer mathRenderer{
            *renderer,
            "external/MicroTeX/res"
        };


        const rose::ui::MathTexture formula =
            mathRenderer.renderDisplayMath(
                R"(\frac{-b \pm \sqrt{b^2 - 4ac}}{2a} = x)",
                980,
                34.0f);


        bool running{
            true
        };


        while (running)
        {
            SDL_Event event{};

            while (SDL_PollEvent(
                &event))
            {
                if (
                    event.type == SDL_EVENT_QUIT
                    || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                {
                    running = false;
                }


                if (
                    event.type == SDL_EVENT_KEY_DOWN
                    && event.key.key == SDLK_ESCAPE)
                {
                    running = false;
                }
            }


            int width{ 0 };
            int height{ 0 };

            SDL_GetWindowSize(
                window,
                &width,
                &height);


            SDL_SetRenderDrawColor(
                renderer,
                25,
                23,
                31,
                255);

            SDL_RenderClear(
                renderer);


            if (formula)
            {
                const SDL_FRect destination{
                    static_cast<float>(
                        width - formula.width())
                        * 0.5f,
                    static_cast<float>(
                        height - formula.height())
                        * 0.5f,
                    static_cast<float>(
                        formula.width()),
                    static_cast<float>(
                        formula.height())
                };


                if (!SDL_RenderTexture(
                    renderer,
                    formula.texture(),
                    nullptr,
                    &destination))
                {
                    throw std::runtime_error{
                        std::string{
                            "Could not draw Rose math texture: "
                        }
                        + SDL_GetError()
                    };
                }
            }


            SDL_RenderPresent(
                renderer);


            std::this_thread::sleep_for(
                std::chrono::milliseconds{
                    16
                });
        }


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Rose Math Sandbox error: "
            << exception.what()
            << '\n';

        return 1;
    }
}
