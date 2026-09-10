#include "platform/SdlRuntime.h"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>


namespace rose::platform
{

    SdlRuntime::SdlRuntime()
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


    SdlRuntime::~SdlRuntime()
    {
        SDL_Quit();
    }

} // namespace rose::platform