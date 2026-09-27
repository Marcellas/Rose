#include "avatar/AvatarPresentationTimeline.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>


namespace
{
    using Timeline =
        rose::avatar::AvatarPresentationTimeline;

    using namespace std::chrono_literals;


    [[noreturn]]
    void fail(
        const std::string_view message)
    {
        std::cerr
            << "Rose AvatarPresentationTimeline test failed: "
            << message
            << '\n';

        std::exit(1);
    }


    void require(
        const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            fail(message);
        }
    }
}


int main()
{
    const Timeline::TimePoint t0{};

    Timeline timeline{
        t0
    };


    // Idle is the seated resting posture.
    auto sample =
        timeline.update(
            rose::avatar::AvatarState::Idle,
            t0,
            2.4f);

    require(
        !sample.transitionActive(),
        "Idle should start as a stable semantic state.");

    require(
        sample.postureProgress == 0.0f,
        "Idle should be seated.");


    // A standing activity inserts a sit -> stand transition instead of snapping.
    sample =
        timeline.update(
            rose::avatar::AvatarState::Thinking,
            t0 + 10ms,
            2.4f);

    require(
        sample.phase
            == rose::avatar::AvatarPresentationPhase::SitToStand,
        "Thinking from Idle should begin sit-to-stand.");


    sample =
        timeline.update(
            rose::avatar::AvatarState::Thinking,
            t0 + 1210ms,
            2.4f);

    require(
        sample.transitionActive(),
        "Sit-to-stand should still be active near its midpoint.");

    require(
        sample.postureProgress > 0.35f
            && sample.postureProgress < 0.65f,
        "Sit-to-stand midpoint should be physically between poses.");


    sample =
        timeline.update(
            rose::avatar::AvatarState::Thinking,
            t0 + 2500ms,
            2.4f);

    require(
        !sample.transitionActive(),
        "Sit-to-stand should complete after the configured duration.");

    require(
        sample.semanticState
            == rose::avatar::AvatarState::Thinking,
        "Thinking should become the displayed state after standing.");

    require(
        sample.postureProgress == 1.0f,
        "Completed sit-to-stand should be fully standing.");


    // A very short semantic state is held long enough to be visible.
    sample =
        timeline.update(
            rose::avatar::AvatarState::Speaking,
            t0 + 2600ms,
            2.4f);

    require(
        sample.semanticState
            == rose::avatar::AvatarState::Thinking,
        "Thinking should respect its minimum visual dwell time.");

    sample =
        timeline.update(
            rose::avatar::AvatarState::Speaking,
            t0 + 3500ms,
            2.4f);

    require(
        sample.semanticState
            == rose::avatar::AvatarState::Speaking,
        "Speaking should take over after the Thinking dwell expires.");


    // Returning to Idle inserts a stand -> sit transition.
    sample =
        timeline.update(
            rose::avatar::AvatarState::Idle,
            t0 + 4550ms,
            2.4f);

    require(
        sample.phase
            == rose::avatar::AvatarPresentationPhase::StandToSit,
        "Idle from Speaking should begin stand-to-sit.");


    // If Rose becomes active again while sitting down, reverse from the current
    // progress rather than snapping to standing or restarting the full clip.
    const float progressBeforeReverse =
        timeline.update(
            rose::avatar::AvatarState::Idle,
            t0 + 5150ms,
            2.4f)
        .postureProgress;

    sample =
        timeline.update(
            rose::avatar::AvatarState::Working,
            t0 + 5160ms,
            2.4f);

    require(
        sample.phase
            == rose::avatar::AvatarPresentationPhase::SitToStand,
        "A new standing request should reverse stand-to-sit in place.");

    require(
        sample.postureProgress
            <= progressBeforeReverse + 0.02f,
        "Transition reversal should preserve the current physical pose.");


    std::cout
        << "Rose AvatarPresentationTimeline tests: PASS\n";

    return 0;
}
