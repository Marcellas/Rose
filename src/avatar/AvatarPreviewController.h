#pragma once

#include "avatar/AvatarState.h"

#include <array>
#include <chrono>
#include <cstddef>

namespace rose::avatar
{
    class SdlAvatar;

    // UI-thread-only temporary presentation overrides used by desktop interaction
    // and the animation demonstration menu. Semantic activity continues to arrive
    // from RoseCore underneath the preview and becomes visible again when the
    // preview ends.
    class AvatarPreviewController final
    {
    public:
        explicit AvatarPreviewController(SdlAvatar& avatar) noexcept;

        void preview(AvatarState state, std::chrono::milliseconds duration);
        void startDemo();
        void stop() noexcept;
        void update();

        [[nodiscard]] bool active() const noexcept;

    private:
        using Clock = std::chrono::steady_clock;

        void applyDemoState();

        SdlAvatar& avatar_;
        std::array<AvatarState, 7> demoStates_{
            AvatarState::Idle,
            AvatarState::Listening,
            AvatarState::Thinking,
            AvatarState::Speaking,
            AvatarState::Working,
            AvatarState::Notification,
            AvatarState::Confused
        };
        std::size_t demoIndex_{ 0 };
        bool demoActive_{ false };
        bool singlePreviewActive_{ false };
        Clock::time_point nextTransition_{};
    };
}
