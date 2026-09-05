#pragma once

#include <cstdint>


namespace rose::avatar
{

    // -----------------------------------------------------------------------------
    // AvatarState
    // -----------------------------------------------------------------------------
    //
    // Presentation-level state for any Rose avatar implementation.
    //
    // This deliberately lives in the Avatar module rather than reusing
    // core::RoseActivity directly.
    //
    // RoseActivity describes what Rose is doing.
    // AvatarState describes what the presentation layer should display.
    //
    // Keeping that translation boundary means a future renderer can evolve without
    // changing RoseCore or the agent/model architecture.
    enum class AvatarState : std::uint8_t
    {
        Idle,
        Listening,
        Thinking,
        Speaking,
        Working,
        Notification,
        Confused
    };

} // namespace rose::avatar