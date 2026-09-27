#pragma once

#include "avatar/AvatarState.h"

#include <chrono>
#include <cstdint>


namespace rose::avatar
{

    // -------------------------------------------------------------------------
    // AvatarPosture
    // -------------------------------------------------------------------------
    //
    // Rose's semantic activity and her physical pose are related, but they are
    // not the same thing. Keeping posture explicit lets the presentation layer
    // insert sit/stand transitions without teaching RoseCore anything about the
    // artwork used to portray those transitions.
    enum class AvatarPosture : std::uint8_t
    {
        Seated,
        Standing
    };


    enum class AvatarPresentationPhase : std::uint8_t
    {
        Semantic,
        SitToStand,
        StandToSit
    };


    struct AvatarPresentationSample
    {
        AvatarPresentationPhase phase{
            AvatarPresentationPhase::Semantic
        };

        // The semantic state that should be shown whenever phase == Semantic.
        AvatarState semanticState{
            AvatarState::Idle
        };

        // Time since the currently displayed semantic state began. This is used
        // by the renderer for the state's loop/one-shot animation and subtle
        // transform effects.
        float semanticElapsedSeconds{ 0.0f };

        // Physical posture progress: 0 = fully seated, 1 = fully standing.
        // During a transition this value moves continuously in either direction.
        float postureProgress{ 0.0f };

        [[nodiscard]]
        bool transitionActive() const noexcept
        {
            return
                phase
                != AvatarPresentationPhase::Semantic;
        }
    };


    // -------------------------------------------------------------------------
    // AvatarPresentationTimeline
    // -------------------------------------------------------------------------
    //
    // Small, renderer-independent state machine that turns rapidly changing
    // semantic AvatarState requests into a physically coherent visual sequence.
    //
    // Example:
    //
    //     Idle (seated)
    //       -> SitToStand
    //       -> Thinking (standing)
    //       -> Speaking (standing)
    //       -> StandToSit
    //       -> Idle (seated)
    //
    // The timeline owns no graphics resources and performs no SDL calls. That
    // makes transition policy testable without a window/GPU and keeps SdlAvatar
    // focused on rendering.
    class AvatarPresentationTimeline final
    {
    public:
        using Clock =
            std::chrono::steady_clock;

        using TimePoint =
            Clock::time_point;


        explicit AvatarPresentationTimeline(
            TimePoint now = Clock::now()) noexcept;


        [[nodiscard]]
        AvatarPresentationSample update(
            AvatarState requestedState,
            TimePoint now,
            float fullPostureTransitionSeconds) noexcept;


        [[nodiscard]]
        static AvatarPosture postureFor(
            AvatarState state) noexcept;


        [[nodiscard]]
        static float minimumVisibleSeconds(
            AvatarState state) noexcept;


    private:
        [[nodiscard]]
        float currentPostureProgress(
            TimePoint now,
            float fullPostureTransitionSeconds) const noexcept;


        void beginPostureTransition(
            float fromProgress,
            float toProgress,
            TimePoint now) noexcept;


        AvatarPresentationPhase phase_{
            AvatarPresentationPhase::Semantic
        };

        AvatarState displayedState_{
            AvatarState::Idle
        };

        AvatarPosture posture_{
            AvatarPosture::Seated
        };

        TimePoint semanticStart_;
        TimePoint transitionStart_;

        float transitionStartProgress_{ 0.0f };
        float transitionTargetProgress_{ 0.0f };
    };

} // namespace rose::avatar
