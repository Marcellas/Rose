#pragma once

#include <cstdint>
#include <functional>


namespace rose::core
{

    // -----------------------------------------------------------------------------
    // RoseActivity
    // -----------------------------------------------------------------------------
    //
    // High-level application activity.
    //
    // These states describe what Rose is doing, not what any particular model,
    // renderer, or UI implementation is doing.
    //
    // The avatar, console frontend, voice layer, and future desktop UI may observe
    // these states without becoming dependencies of RoseCore.
    enum class RoseActivity : std::uint8_t
    {
        Idle,
        Listening,
        Thinking,
        Speaking,
        Working,
        Notification,
        Confused
    };


    // -----------------------------------------------------------------------------
    // RoseActivityCallback
    // -----------------------------------------------------------------------------
    //
    // Observers receive transient state changes.
    //
    // The callback does not own RoseCore and RoseCore does not own the observer.
    // A frontend supplies the callback for the duration of the operation.
    using RoseActivityCallback =
        std::function<void(RoseActivity)>;

} // namespace rose::core