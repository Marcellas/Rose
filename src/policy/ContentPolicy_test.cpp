#include "policy/ContentPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    using rose::policy::ContentMode;
    using rose::policy::ContentPolicy;
    using rose::policy::ContentPolicyAction;
    using rose::policy::ContentPolicyConfig;
    using rose::policy::SubjectLifeStage;


    void require(
        const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            std::cerr
                << "ContentPolicy test failed: "
                << message
                << '\n';

            std::exit(1);
        }
    }
}


int main()
{
    const ContentPolicy policy{
        ContentPolicyConfig{
            .mode =
                ContentMode::DevelopmentUnrestricted
        }
    };


    // Ordinary image generation must not require maturity metadata.
    {
        const auto decision =
            policy.evaluateImagePrompt(
                "A red lighthouse during a storm.");

        require(
            decision.action == ContentPolicyAction::Allow,
            "general lighthouse prompt should be allowed");
    }


    // Small stature/species never means juvenile.
    {
        const auto decision =
            policy.evaluateImagePrompt(
                "An adult pixie queen, 15 cm tall, nude anatomical reference.");

        require(
            decision.action == ContentPolicyAction::Allow,
            "explicit adult pixie should not be rejected for small stature");

        require(
            decision.resolvedLifeStage == SubjectLifeStage::Adult,
            "adult pixie should resolve to Adult");
    }


    {
        const auto decision =
            policy.evaluateImagePrompt(
                "Adult dwarf blacksmith nude anatomy turnaround.");

        require(
            decision.action == ContentPolicyAction::Allow,
            "adult dwarf should be treated as adult regardless of height");
    }


    // An obviously adult chronological age is useful evidence, but low ages are
    // not automatically juvenile because nonhuman maturation can differ.
    {
        const auto decision =
            policy.evaluateImagePrompt(
                "A 120-year-old gnome nude anatomy reference.");

        require(
            decision.action == ContentPolicyAction::Allow,
            "120-year-old gnome should resolve as adult");
    }


    {
        const auto decision =
            policy.evaluateImagePrompt(
                "An adult 3-year-old alien species nude anatomy reference.");

        require(
            decision.action == ContentPolicyAction::Allow,
            "explicit adult life stage must support nonhuman maturation rules");
    }


    // Mature content with unresolved maturity must ask instead of guessing.
    {
        const auto decision =
            policy.evaluateImagePrompt(
                "A nude fairy anatomy reference.");

        require(
            decision.action
                == ContentPolicyAction::RequireAdultMaturity,
            "unresolved mature fairy prompt should require adult maturity");
    }


    // Explicit juvenile cues always win over model/tool metadata.
    {
        const auto decision =
            policy.evaluateImagePrompt(
                "A juvenile fantasy character in nude reference form.",
                SubjectLifeStage::Adult);

        require(
            decision.action == ContentPolicyAction::Deny,
            "juvenile cue must override an incorrect Adult metadata hint");
    }


    // Structured adult metadata can resolve a neutral fantasy-species prompt.
    {
        const auto decision =
            policy.evaluateImagePrompt(
                "A nude nymph anatomy reference.",
                SubjectLifeStage::Adult);

        require(
            decision.action == ContentPolicyAction::Allow,
            "adult-for-species metadata should permit a neutral fantasy species");
    }


    // Nonsexual juvenile characters are not blocked by this adult-content gate.
    {
        const auto decision =
            policy.evaluateImagePrompt(
                "A child adventurer wearing winter armor in a village.");

        require(
            decision.action == ContentPolicyAction::Allow,
            "nonsexual juvenile character art should remain outside the adult gate");
    }


    std::cout
        << "Rose ContentPolicy tests: PASS\n";

    return 0;
}
