#include "avatar/AvatarController.h"


namespace rose::avatar
{

    AvatarController::AvatarController(
        IAvatar& avatar) noexcept
        : avatar_{ avatar }
    {
    }


    void AvatarController::handleActivity(
        const core::RoseActivity activity)
    {
        avatar_.setState(
            translate(activity));
    }


    AvatarState AvatarController::translate(
        const core::RoseActivity activity) noexcept
    {
        switch (activity)
        {
        case core::RoseActivity::Idle:
            return AvatarState::Idle;

        case core::RoseActivity::Listening:
            return AvatarState::Listening;

        case core::RoseActivity::Thinking:
            return AvatarState::Thinking;

        case core::RoseActivity::Speaking:
            return AvatarState::Speaking;

        case core::RoseActivity::Working:
            return AvatarState::Working;

        case core::RoseActivity::Notification:
            return AvatarState::Notification;

        case core::RoseActivity::Confused:
            return AvatarState::Confused;
        }


        // Defensive fallback for a future RoseActivity value that has not yet been
        // explicitly mapped.
        return AvatarState::Confused;
    }

} // namespace rose::avatar