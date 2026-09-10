#include "avatar/AvatarController.h"
#include "avatar/SdlAvatar.h"
#include "platform/SdlRuntime.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <thread>


int main()
{
    try
    {
        rose::platform::SdlRuntime sdlRuntime;

        rose::avatar::SdlAvatar avatar{
            sdlRuntime,
            320,
            300
        };

        avatar.loadSprite(
            "assets/avatar/RoseIdle.png");

        rose::avatar::AvatarController controller{
            avatar
        };


        using Clock =
            std::chrono::steady_clock;


        constexpr std::chrono::milliseconds stateDuration{
            1200
        };


        const rose::core::RoseActivity states[]{
            rose::core::RoseActivity::Idle,
            rose::core::RoseActivity::Listening,
            rose::core::RoseActivity::Thinking,
            rose::core::RoseActivity::Speaking,
            rose::core::RoseActivity::Working,
            rose::core::RoseActivity::Notification,
            rose::core::RoseActivity::Confused
        };


        std::size_t stateIndex{ 0 };


        controller.handleActivity(
            states[stateIndex]);


        auto nextStateChange =
            Clock::now()
            + stateDuration;


        bool running{ true };


        while (running)
        {
            // -------------------------------------------------------------------------
            // Central SDL event pump
            // -------------------------------------------------------------------------
            //
            // SdlAvatar no longer owns SDL_PollEvent().
            //
            // Even this small sandbox follows the same architecture as Rose:
            //
            //     application
            //         |
            //         +-- SDL_PollEvent()
            //                 |
            //                 v
            //          avatar.handleEvent()
            SDL_Event event{};


            while (SDL_PollEvent(
                &event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    running = false;

                    break;
                }


                running =
                    avatar.handleEvent(
                        event);


                if (!running)
                {
                    break;
                }
            }


            if (!running)
            {
                break;
            }


            const auto now =
                Clock::now();


            if (now >= nextStateChange)
            {
                stateIndex =
                    (stateIndex + 1)
                    % std::size(states);


                controller.handleActivity(
                    states[stateIndex]);


                nextStateChange =
                    now
                    + stateDuration;
            }


            avatar.render();


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
            << "Avatar sandbox error: "
            << exception.what()
            << '\n';

        return 1;
    }
}