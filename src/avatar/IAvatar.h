#pragma once

#include "avatar/AvatarState.h"


namespace rose::avatar
{

    // -----------------------------------------------------------------------------
    // IAvatar
    // -----------------------------------------------------------------------------
    //
    // Minimal presentation interface for Rose's avatar.
    //
    // RoseCore does NOT know this interface exists.
    //
    // Concrete implementations might eventually include:
    //
    //     ConsoleAvatar
    //     SdlAvatar
    //     Win32Avatar
    //     HeadlessAvatar
    //
    // OWNERSHIP
    // ---------
    // The interface owns nothing.
    //
    // Whoever constructs the concrete avatar controls its lifetime.
    class IAvatar
    {
    public:
        virtual ~IAvatar() = default;

        virtual void setState(
            AvatarState state) = 0;
    };

} // namespace rose::avatar