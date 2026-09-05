#pragma once

#include "avatar/IAvatar.h"
#include "core/RoseActivity.h"


namespace rose::avatar
{

    // -----------------------------------------------------------------------------
    // AvatarController
    // -----------------------------------------------------------------------------
    //
    // Converts Rose's application-level activity into presentation-level avatar
    // state.
    //
    // Dependency direction:
    //
    //     RoseCore
    //         |
    //         | emits RoseActivity
    //         v
    //     AvatarController
    //         |
    //         | translates
    //         v
    //     IAvatar
    //
    // RoseCore therefore remains completely independent from rendering.
    class AvatarController final
    {
    public:
        explicit AvatarController(
            IAvatar& avatar) noexcept;


        void handleActivity(
            core::RoseActivity activity);


    private:
        [[nodiscard]]
        static AvatarState translate(
            core::RoseActivity activity) noexcept;


        // Borrowed.
        //
        // The concrete avatar must outlive AvatarController.
        IAvatar& avatar_;
    };

} // namespace rose::avatar