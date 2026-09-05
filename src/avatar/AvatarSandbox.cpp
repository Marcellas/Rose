#include "avatar/AvatarController.h"
#include "avatar/SdlAvatar.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <thread>


int main()
{
    try
    {
        rose::avatar::SdlAvatar avatar{
            320,
            300
        };


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
            running =
                avatar.processEvents();


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


            // Keep the sandbox responsive without consuming a full CPU core.
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