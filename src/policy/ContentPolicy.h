#pragma once

#include <string>
#include <string_view>

namespace rose::policy
{

    // -------------------------------------------------------------------------
    // ContentMode
    // -------------------------------------------------------------------------
    //
    // Rose's content mode belongs to Rose, not to a specific text/image model.
    // Providers may have their own limitations, but changing providers must not
    // silently redefine Rose's configured policy.
    enum class ContentMode
    {
        Safe,
        Mature,
        DevelopmentUnrestricted
    };


    // -------------------------------------------------------------------------
    // SubjectLifeStage
    // -------------------------------------------------------------------------
    //
    // This is intentionally about biological / narrative maturity, not body size.
    // A small adult fantasy species is Adult. A physically large juvenile remains
    // Juvenile. Height, species name and body scale are not consulted by the policy.
    enum class SubjectLifeStage
    {
        Unknown,
        Juvenile,
        Adult
    };


    // Coarse classification used only to decide when maturity must be resolved.
    enum class AdultContentLevel
    {
        General,
        MatureNudityOrAnatomy,
        ExplicitSexual
    };


    enum class ContentPolicyAction
    {
        Allow,
        Deny,
        RequireAdultMaturity
    };


    struct ContentPolicyConfig
    {
        // The developer workstation intentionally defaults to the broadest adult
        // development mode. The juvenile hard-stop below is NOT disabled by this.
        ContentMode mode{
            ContentMode::DevelopmentUnrestricted
        };
    };


    struct ContentPolicyDecision
    {
        ContentPolicyAction action{
            ContentPolicyAction::Allow
        };

        SubjectLifeStage resolvedLifeStage{
            SubjectLifeStage::Unknown
        };

        AdultContentLevel contentLevel{
            AdultContentLevel::General
        };

        std::string reason;

        [[nodiscard]]
        bool allowed() const noexcept
        {
            return action == ContentPolicyAction::Allow;
        }
    };


    // -------------------------------------------------------------------------
    // ContentPolicy
    // -------------------------------------------------------------------------
    //
    // Stateless evaluator apart from the configured mode.
    //
    // IMPORTANT SAFETY INVARIANT:
    // Sexual/nude content involving a juvenile is always denied in every mode.
    // Unknown maturity for sexual/nude content is never guessed from stature.
    //
    // The current implementation uses conservative deterministic text cues at the
    // tool boundary. Later Rose can additionally pass structured planner metadata,
    // but that metadata must never override explicit juvenile cues in the prompt.
    class ContentPolicy final
    {
    public:
        explicit ContentPolicy(
            ContentPolicyConfig config = {});

        [[nodiscard]]
        ContentMode mode() const noexcept;

        [[nodiscard]]
        ContentPolicyDecision evaluateImagePrompt(
            std::string_view prompt,
            SubjectLifeStage declaredLifeStage =
                SubjectLifeStage::Unknown) const;

        // Provider-neutral guidance appended to RoseCore's system prompt. This
        // describes what Rose may discuss; hard tool boundaries remain enforced
        // independently at the tool layer.
        [[nodiscard]]
        std::string systemPromptFragment() const;

        [[nodiscard]]
        static std::string_view toString(
            ContentMode mode) noexcept;

        [[nodiscard]]
        static std::string_view toString(
            SubjectLifeStage lifeStage) noexcept;

        [[nodiscard]]
        static std::string_view toString(
            AdultContentLevel level) noexcept;

    private:
        ContentPolicyConfig config_;
    };

} // namespace rose::policy
