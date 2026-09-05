#include "avatar/ConsoleAvatar.h"

#include <iostream>


namespace rose::avatar
{

    void ConsoleAvatar::setState(
        const AvatarState state)
    {
        const char* name = "Unknown";

        switch (state)
        {
        case AvatarState::Idle:
            name = "Idle";
            break;

        case AvatarState::Listening:
            name = "Listening";
            break;

        case AvatarState::Thinking:
            name = "Thinking";
            break;

        case AvatarState::Speaking:
            name = "Speaking";
            break;

        case AvatarState::Working:
            name = "Working";
            break;

        case AvatarState::Notification:
            name = "Notification";
            break;

        case AvatarState::Confused:
            name = "Confused";
            break;
        }


        std::cerr
            << "\n[Avatar: "
            << name
            << "]\n";
    }

} // namespace rose::avatar