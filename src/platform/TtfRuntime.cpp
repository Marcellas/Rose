#include "platform/TtfRuntime.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdexcept>
#include <string>


namespace rose::platform
{

    TtfRuntime::TtfRuntime()
    {
        if (!TTF_Init())
        {
            throw std::runtime_error{
                std::string{
                    "SDL_ttf initialization failed: "
                }
                + SDL_GetError()
            };
        }
    }


    TtfRuntime::~TtfRuntime()
    {
        TTF_Quit();
    }

} // namespace rose::platform