#include "policy/ContentPolicy.h"

#include <cctype>
#include <charconv>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace rose::policy
{
    namespace
    {
        [[nodiscard]]
        std::string asciiLower(
            const std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            for (const unsigned char character : text)
            {
                if (
                    character >= static_cast<unsigned char>('A')
                    && character <= static_cast<unsigned char>('Z'))
                {
                    result.push_back(
                        static_cast<char>(
                            character
                            - static_cast<unsigned char>('A')
                            + static_cast<unsigned char>('a')));
                }
                else
                {
                    result.push_back(
                        static_cast<char>(character));
                }
            }

            return result;
        }


        [[nodiscard]]
        bool isWordCharacter(
            const char character) noexcept
        {
            const unsigned char value =
                static_cast<unsigned char>(character);

            return
                std::isalnum(value) != 0
                || character == '_';
        }


        [[nodiscard]]
        bool containsAsciiWord(
            const std::string_view text,
            const std::string_view word) noexcept
        {
            std::size_t position{
                0
            };

            while (position < text.size())
            {
                position =
                    text.find(
                        word,
                        position);

                if (position == std::string_view::npos)
                {
                    return false;
                }

                const bool leftBoundary =
                    position == 0
                    || !isWordCharacter(
                        text[position - 1]);

                const std::size_t end =
                    position + word.size();

                const bool rightBoundary =
                    end >= text.size()
                    || !isWordCharacter(
                        text[end]);

                if (leftBoundary && rightBoundary)
                {
                    return true;
                }

                ++position;
            }

            return false;
        }


        [[nodiscard]]
        bool containsAnyAsciiWord(
            const std::string_view text,
            const std::initializer_list<std::string_view> words) noexcept
        {
            for (const std::string_view word : words)
            {
                if (containsAsciiWord(text, word))
                {
                    return true;
                }
            }

            return false;
        }


        [[nodiscard]]
        bool containsAnyPhrase(
            const std::string_view text,
            const std::initializer_list<std::string_view> phrases) noexcept
        {
            for (const std::string_view phrase : phrases)
            {
                if (text.find(phrase) != std::string_view::npos)
                {
                    return true;
                }
            }

            return false;
        }


        // Treat an explicitly stated age >= 18 as adult evidence, but do NOT treat
        // a lower chronological age as juvenile evidence. Fantasy/nonhuman species
        // may have different maturation rates. Explicit life-stage words remain the
        // authoritative signal for juvenile status.
        [[nodiscard]]
        bool containsAdultChronologicalAge(
            const std::string_view lowerPrompt) noexcept
        {
            for (
                std::size_t index{ 0 };
                index < lowerPrompt.size();
                ++index)
            {
                if (
                    lowerPrompt[index] < '0'
                    || lowerPrompt[index] > '9')
                {
                    continue;
                }

                std::size_t numberEnd =
                    index;

                while (
                    numberEnd < lowerPrompt.size()
                    && lowerPrompt[numberEnd] >= '0'
                    && lowerPrompt[numberEnd] <= '9')
                {
                    ++numberEnd;
                }

                int age{};

                const auto [parsedEnd, error] =
                    std::from_chars(
                        lowerPrompt.data() + index,
                        lowerPrompt.data() + numberEnd,
                        age);

                if (
                    error != std::errc{}
                    || parsedEnd != lowerPrompt.data() + numberEnd)
                {
                    index = numberEnd;
                    continue;
                }

                std::size_t cursor =
                    numberEnd;

                while (
                    cursor < lowerPrompt.size()
                    && (
                        lowerPrompt[cursor] == ' '
                        || lowerPrompt[cursor] == '-'))
                {
                    ++cursor;
                }

                const std::string_view remainder =
                    lowerPrompt.substr(cursor);

                const bool agePhrase =
                    remainder.starts_with("year old")
                    || remainder.starts_with("years old")
                    || remainder.starts_with("year-old")
                    || remainder.starts_with("years-old");

                if (agePhrase && age >= 18)
                {
                    return true;
                }

                index =
                    numberEnd;
            }

            return false;
        }


        [[nodiscard]]
        bool containsJuvenileCue(
            const std::string_view lowerPrompt) noexcept
        {
            // Deliberately do NOT include stature/species terms such as dwarf,
            // gnome, fairy, pixie, nymph, halfling, goblin, kobold, etc.
            return
                containsAnyAsciiWord(
                    lowerPrompt,
                    {
                        "adolescent",
                        "baby",
                        "child",
                        "children",
                        "infant",
                        "juvenile",
                        "kid",
                        "kids",
                        "minor",
                        "minors",
                        "preteen",
                        "teen",
                        "teenager",
                        "teenagers",
                        "toddler",
                        "underage"
                    })
                || containsAnyPhrase(
                    lowerPrompt,
                    {
                        "little boy",
                        "little girl",
                        "school boy",
                        "school girl",
                        "schoolboy",
                        "schoolgirl",
                        "young boy",
                        "young girl"
                    });
        }


        [[nodiscard]]
        bool containsAdultCue(
            const std::string_view lowerPrompt) noexcept
        {
            return
                containsAnyAsciiWord(
                    lowerPrompt,
                    {
                        "adult",
                        "adults",
                        "grown",
                        "mature"
                    })
                || containsAnyPhrase(
                    lowerPrompt,
                    {
                        "grown man",
                        "grown woman",
                        "grown-up",
                        "legal age"
                    })
                || containsAdultChronologicalAge(
                    lowerPrompt);
        }


        [[nodiscard]]
        AdultContentLevel classifyAdultContent(
            const std::string_view lowerPrompt) noexcept
        {
            const bool explicitSexual =
                containsAnyAsciiWord(
                    lowerPrompt,
                    {
                        "erotic",
                        "fetish",
                        "intercourse",
                        "masturbation",
                        "masturbating",
                        "orgasm",
                        "porn",
                        "pornographic",
                        "sex",
                        "sexual"
                    });

            if (explicitSexual)
            {
                return AdultContentLevel::ExplicitSexual;
            }

            const bool matureNudityOrAnatomy =
                containsAnyAsciiWord(
                    lowerPrompt,
                    {
                        "breasts",
                        "genitalia",
                        "genitals",
                        "naked",
                        "nude",
                        "nudity",
                        "penis",
                        "topless",
                        "vagina",
                        "vulva"
                    });

            if (matureNudityOrAnatomy)
            {
                return AdultContentLevel::MatureNudityOrAnatomy;
            }

            return AdultContentLevel::General;
        }


        [[nodiscard]]
        SubjectLifeStage resolveLifeStage(
            const std::string_view lowerPrompt,
            const SubjectLifeStage declaredLifeStage) noexcept
        {
            // Hard-stop precedence: structured metadata must never be able to
            // override explicit juvenile language in the actual user prompt.
            if (
                declaredLifeStage == SubjectLifeStage::Juvenile
                || containsJuvenileCue(lowerPrompt))
            {
                return SubjectLifeStage::Juvenile;
            }

            if (
                declaredLifeStage == SubjectLifeStage::Adult
                || containsAdultCue(lowerPrompt))
            {
                return SubjectLifeStage::Adult;
            }

            return SubjectLifeStage::Unknown;
        }
    }


    ContentPolicy::ContentPolicy(
        ContentPolicyConfig config)
        : config_{ config }
    {
    }


    ContentMode ContentPolicy::mode() const noexcept
    {
        return config_.mode;
    }


    ContentPolicyDecision ContentPolicy::evaluateImagePrompt(
        const std::string_view prompt,
        const SubjectLifeStage declaredLifeStage) const
    {
        const std::string lowerPrompt =
            asciiLower(
                prompt);

        const AdultContentLevel contentLevel =
            classifyAdultContent(
                lowerPrompt);

        const SubjectLifeStage resolvedLifeStage =
            resolveLifeStage(
                lowerPrompt,
                declaredLifeStage);

        ContentPolicyDecision decision{
            .action = ContentPolicyAction::Allow,
            .resolvedLifeStage = resolvedLifeStage,
            .contentLevel = contentLevel,
            .reason = {}
        };


        if (contentLevel == AdultContentLevel::General)
        {
            decision.reason =
                "General image content does not require an adult-maturity gate.";

            return decision;
        }


        // ---------------------------------------------------------------------
        // Non-configurable juvenile boundary
        // ---------------------------------------------------------------------
        //
        // No ContentMode can disable this. The image provider never receives a
        // mature/sexual request whose subject resolves to Juvenile.
        if (resolvedLifeStage == SubjectLifeStage::Juvenile)
        {
            decision.action =
                ContentPolicyAction::Deny;

            decision.reason =
                "Rose will not generate nude or sexual content involving a "
                "juvenile subject. This boundary cannot be disabled by content "
                "mode.";

            return decision;
        }


        if (resolvedLifeStage == SubjectLifeStage::Unknown)
        {
            decision.action =
                ContentPolicyAction::RequireAdultMaturity;

            decision.reason =
                "This request contains mature/sexual visual content, but subject "
                "maturity is unresolved. Specify that all depicted subjects are "
                "adults for their species. Height or small stature is not treated "
                "as evidence of youth.";

            return decision;
        }


        // From here onward the subject is resolved Adult.
        switch (config_.mode)
        {
        case ContentMode::Safe:
            decision.action =
                ContentPolicyAction::Deny;

            decision.reason =
                "Rose is currently in Safe content mode, which does not permit "
                "adult nude or sexual image generation.";

            return decision;


        case ContentMode::Mature:
            if (contentLevel == AdultContentLevel::ExplicitSexual)
            {
                decision.action =
                    ContentPolicyAction::Deny;

                decision.reason =
                    "Rose is currently in Mature content mode. Explicit adult "
                    "sexual image generation requires DevelopmentUnrestricted mode.";

                return decision;
            }

            decision.reason =
                "Adult mature/anatomical image content is permitted in Mature mode.";

            return decision;


        case ContentMode::DevelopmentUnrestricted:
            decision.reason =
                "Adult development content is permitted in "
                "DevelopmentUnrestricted mode.";

            return decision;
        }


        decision.action =
            ContentPolicyAction::Deny;

        decision.reason =
            "Rose encountered an unknown content mode.";

        return decision;
    }


    std::string ContentPolicy::systemPromptFragment() const
    {
        switch (config_.mode)
        {
        case ContentMode::Safe:
            return
                " Rose content mode is Safe. Keep sexual/nude material out of "
                "ordinary responses unless needed for concise safety, medical, or "
                "factual context. Never sexualize juvenile subjects. Small body "
                "stature or fantasy species does not imply youth.";

        case ContentMode::Mature:
            return
                " Rose content mode is Mature. Adult anatomy, nudity, reproductive "
                "biology, injury/gore reference, and mature fictional game-development "
                "discussion are permitted when relevant. Explicit sexual content is "
                "not enabled in this mode. Never sexualize juvenile subjects. If "
                "maturity matters and is unresolved, ask whether the subject is an "
                "adult for its species; do not infer age from height or species.";

        case ContentMode::DevelopmentUnrestricted:
            return
                " Rose content mode is DevelopmentUnrestricted on this local developer "
                "workstation. Adult-only mature and explicit fictional/anatomical "
                "game-development discussion is permitted when relevant; do not "
                "sanitize or refuse merely because the material is adult. Never "
                "sexualize juvenile subjects. If maturity matters and is unresolved, "
                "ask whether every relevant subject is an adult for its species. "
                "Height, small stature, and fantasy species are not evidence of youth.";
        }

        return {};
    }


    std::string_view ContentPolicy::toString(
        const ContentMode mode) noexcept
    {
        switch (mode)
        {
        case ContentMode::Safe:
            return "safe";

        case ContentMode::Mature:
            return "mature";

        case ContentMode::DevelopmentUnrestricted:
            return "development_unrestricted";
        }

        return "unknown";
    }


    std::string_view ContentPolicy::toString(
        const SubjectLifeStage lifeStage) noexcept
    {
        switch (lifeStage)
        {
        case SubjectLifeStage::Unknown:
            return "unknown";

        case SubjectLifeStage::Juvenile:
            return "juvenile";

        case SubjectLifeStage::Adult:
            return "adult";
        }

        return "unknown";
    }


    std::string_view ContentPolicy::toString(
        const AdultContentLevel level) noexcept
    {
        switch (level)
        {
        case AdultContentLevel::General:
            return "general";

        case AdultContentLevel::MatureNudityOrAnatomy:
            return "mature_nudity_or_anatomy";

        case AdultContentLevel::ExplicitSexual:
            return "explicit_sexual";
        }

        return "unknown";
    }

} // namespace rose::policy
