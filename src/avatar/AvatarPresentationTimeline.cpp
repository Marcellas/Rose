#include "avatar/AvatarPresentationTimeline.h"

#include <algorithm>
#include <cmath>


namespace rose::avatar
{

    namespace
    {
        [[nodiscard]]
        float secondsBetween(
            const AvatarPresentationTimeline::TimePoint start,
            const AvatarPresentationTimeline::TimePoint end) noexcept
        {
            return
                std::max(
                    0.0f,
                    std::chrono::duration<float>(
                        end - start)
                    .count());
        }


        [[nodiscard]]
        constexpr float postureProgressFor(
            const AvatarPosture posture) noexcept
        {
            return
                posture == AvatarPosture::Standing
                    ? 1.0f
                    : 0.0f;
        }
    }


    AvatarPresentationTimeline::AvatarPresentationTimeline(
        const TimePoint now) noexcept
        : semanticStart_{ now }
        , transitionStart_{ now }
    {
    }


    AvatarPresentationSample AvatarPresentationTimeline::update(
        const AvatarState requestedState,
        const TimePoint now,
        const float fullPostureTransitionSeconds) noexcept
    {
        const float safeTransitionSeconds =
            std::max(
                0.001f,
                fullPostureTransitionSeconds);

        const AvatarPosture requestedPosture =
            postureFor(
                requestedState);


        // ---------------------------------------------------------------------
        // Existing sit/stand transition
        // ---------------------------------------------------------------------
        if (
            phase_
            != AvatarPresentationPhase::Semantic)
        {
            float progress =
                currentPostureProgress(
                    now,
                    safeTransitionSeconds);

            const float requestedTarget =
                postureProgressFor(
                    requestedPosture);


            // If Rose's requested activity changes posture while she is already
            // moving, reverse from the exact current progress rather than snapping
            // to an endpoint first. A quick command can therefore make her start
            // standing, reconsider, and naturally sit back down.
            if (
                std::abs(
                    requestedTarget
                    - transitionTargetProgress_)
                > 0.0001f)
            {
                beginPostureTransition(
                    progress,
                    requestedTarget,
                    now);

                progress =
                    currentPostureProgress(
                        now,
                        safeTransitionSeconds);
            }


            const bool reachedTarget =
                std::abs(
                    progress
                    - transitionTargetProgress_)
                <= 0.0001f;

            if (reachedTarget)
            {
                posture_ =
                    transitionTargetProgress_
                    >= 0.5f
                        ? AvatarPosture::Standing
                        : AvatarPosture::Seated;

                phase_ =
                    AvatarPresentationPhase::Semantic;

                // Use the newest requested state once the physical transition is
                // complete. Intermediate standing states do not need to be queued;
                // Rose lands on whichever activity is actually current now.
                if (
                    postureFor(
                        requestedState)
                    == posture_)
                {
                    displayedState_ =
                        requestedState;
                }

                semanticStart_ =
                    now;

                return AvatarPresentationSample{
                    .phase = AvatarPresentationPhase::Semantic,
                    .semanticState = displayedState_,
                    .semanticElapsedSeconds = 0.0f,
                    .postureProgress =
                        postureProgressFor(
                            posture_)
                };
            }


            return AvatarPresentationSample{
                .phase = phase_,
                .semanticState = displayedState_,
                .semanticElapsedSeconds =
                    secondsBetween(
                        semanticStart_,
                        now),
                .postureProgress = progress
            };
        }


        // ---------------------------------------------------------------------
        // Stable semantic state
        // ---------------------------------------------------------------------
        const float semanticElapsed =
            secondsBetween(
                semanticStart_,
                now);

        const bool mayLeaveDisplayedState =
            semanticElapsed
            >= minimumVisibleSeconds(
                displayedState_);


        if (
            requestedPosture
            != posture_)
        {
            if (mayLeaveDisplayedState)
            {
                beginPostureTransition(
                    postureProgressFor(
                        posture_),
                    postureProgressFor(
                        requestedPosture),
                    now);

                return AvatarPresentationSample{
                    .phase = phase_,
                    .semanticState = displayedState_,
                    .semanticElapsedSeconds = semanticElapsed,
                    .postureProgress =
                        postureProgressFor(
                            posture_)
                };
            }

            return AvatarPresentationSample{
                .phase = AvatarPresentationPhase::Semantic,
                .semanticState = displayedState_,
                .semanticElapsedSeconds = semanticElapsed,
                .postureProgress =
                    postureProgressFor(
                        posture_)
            };
        }


        if (
            requestedState
            != displayedState_
            && mayLeaveDisplayedState)
        {
            displayedState_ =
                requestedState;

            semanticStart_ =
                now;

            return AvatarPresentationSample{
                .phase = AvatarPresentationPhase::Semantic,
                .semanticState = displayedState_,
                .semanticElapsedSeconds = 0.0f,
                .postureProgress =
                    postureProgressFor(
                        posture_)
            };
        }


        return AvatarPresentationSample{
            .phase = AvatarPresentationPhase::Semantic,
            .semanticState = displayedState_,
            .semanticElapsedSeconds = semanticElapsed,
            .postureProgress =
                postureProgressFor(
                    posture_)
        };
    }


    AvatarPosture AvatarPresentationTimeline::postureFor(
        const AvatarState state) noexcept
    {
        switch (state)
        {
        case AvatarState::Idle:
        case AvatarState::Confused:
            return AvatarPosture::Seated;

        case AvatarState::Listening:
        case AvatarState::Thinking:
        case AvatarState::Speaking:
        case AvatarState::Working:
        case AvatarState::Notification:
            return AvatarPosture::Standing;
        }


        return AvatarPosture::Seated;
    }


    float AvatarPresentationTimeline::minimumVisibleSeconds(
        const AvatarState state) noexcept
    {
        // These are presentation-only dwell times. They never delay Rose's model,
        // tools, file operations, or responses; they only prevent a visual state
        // from flashing by too quickly for a person to perceive.
        switch (state)
        {
        case AvatarState::Idle:
            return 0.0f;

        case AvatarState::Listening:
            return 0.65f;

        case AvatarState::Thinking:
            return 0.85f;

        case AvatarState::Speaking:
            return 1.00f;

        case AvatarState::Working:
            return 0.85f;

        case AvatarState::Notification:
            return 3.10f;

        case AvatarState::Confused:
            return 1.20f;
        }


        return 0.0f;
    }


    float AvatarPresentationTimeline::currentPostureProgress(
        const TimePoint now,
        const float fullPostureTransitionSeconds) const noexcept
    {
        const float distance =
            std::abs(
                transitionTargetProgress_
                - transitionStartProgress_);

        if (distance <= 0.0001f)
        {
            return transitionTargetProgress_;
        }

        // A reversal may begin halfway through the movement. Scale its duration by
        // the remaining distance so speed is continuous instead of making half a
        // transition take the same time as a full sit/stand cycle.
        const float segmentDuration =
            std::max(
                0.001f,
                fullPostureTransitionSeconds
                * distance);

        const float elapsed =
            secondsBetween(
                transitionStart_,
                now);

        const float t =
            std::clamp(
                elapsed
                / segmentDuration,
                0.0f,
                1.0f);

        // Smoothstep gives the posture motion a gentle acceleration/deceleration
        // while preserving exact endpoints and reversal continuity.
        const float eased =
            t
            * t
            * (3.0f - 2.0f * t);

        return
            transitionStartProgress_
            + (
                transitionTargetProgress_
                - transitionStartProgress_)
            * eased;
    }


    void AvatarPresentationTimeline::beginPostureTransition(
        const float fromProgress,
        const float toProgress,
        const TimePoint now) noexcept
    {
        transitionStartProgress_ =
            std::clamp(
                fromProgress,
                0.0f,
                1.0f);

        transitionTargetProgress_ =
            std::clamp(
                toProgress,
                0.0f,
                1.0f);

        transitionStart_ =
            now;

        phase_ =
            transitionTargetProgress_
            >= transitionStartProgress_
                ? AvatarPresentationPhase::SitToStand
                : AvatarPresentationPhase::StandToSit;
    }

} // namespace rose::avatar
