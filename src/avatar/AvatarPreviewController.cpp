#include "avatar/AvatarPreviewController.h"

#include "avatar/SdlAvatar.h"

namespace rose::avatar
{
    AvatarPreviewController::AvatarPreviewController(SdlAvatar& avatar) noexcept
        : avatar_{ avatar }
    {
    }

    void AvatarPreviewController::preview(
        const AvatarState state,
        const std::chrono::milliseconds duration)
    {
        demoActive_ = false;
        singlePreviewActive_ = true;
        avatar_.setPresentationOverride(state);
        nextTransition_ = Clock::now() + duration;
    }

    void AvatarPreviewController::startDemo()
    {
        singlePreviewActive_ = false;
        demoActive_ = true;
        demoIndex_ = 0;
        applyDemoState();
    }

    void AvatarPreviewController::stop() noexcept
    {
        demoActive_ = false;
        singlePreviewActive_ = false;
        avatar_.setPresentationOverride(std::nullopt);
    }

    void AvatarPreviewController::update()
    {
        if (!demoActive_ && !singlePreviewActive_)
        {
            return;
        }

        if (Clock::now() < nextTransition_)
        {
            return;
        }

        if (singlePreviewActive_)
        {
            stop();
            return;
        }

        ++demoIndex_;
        if (demoIndex_ >= demoStates_.size())
        {
            stop();
            return;
        }

        applyDemoState();
    }

    bool AvatarPreviewController::active() const noexcept
    {
        return demoActive_ || singlePreviewActive_;
    }

    void AvatarPreviewController::applyDemoState()
    {
        avatar_.setPresentationOverride(demoStates_[demoIndex_]);
        nextTransition_ = Clock::now() + std::chrono::milliseconds{ 1800 };
    }
}
