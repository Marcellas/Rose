#pragma once

#include "avatar/IAvatar.h"


namespace rose::avatar
{

    // -----------------------------------------------------------------------------
    // ConsoleAvatar
    // -----------------------------------------------------------------------------
    //
    // Development/headless implementation of IAvatar.
    //
    // This is useful even after the graphical avatar exists because Rose can still
    // run in console/headless diagnostic environments.
    class ConsoleAvatar final
        : public IAvatar
    {
    public:
        void setState(
            AvatarState state) override;
    };

} // namespace rose::avatar