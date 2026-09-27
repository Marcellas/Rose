#pragma once

#include "imagegen/ImageGenerationTypes.h"

#include <string>
#include <string_view>

namespace rose::imagegen
{

    // User/Agent-facing quality intent.
    //
    // These values are provider-neutral. Rose chooses a quality level; the
    // active provider/profile set decides concrete resolution/sampling values.
    enum class ImageGenerationQuality
    {
        Draft,
        Standard,
        High
    };


    // User/Agent-facing composition intent.
    enum class ImageAspectRatio
    {
        Auto,
        Square,
        Portrait,
        Landscape,
        Wide
    };


    // Semantic request produced by Rose/tool routing.
    //
    // Agent-facing code should prefer these fields instead of diffusion
    // implementation details such as raw sampling steps or CFG scale.
    struct ImageGenerationIntent
    {
        std::string prompt;
        std::string negativePrompt;

        ImageGenerationQuality quality{
            ImageGenerationQuality::Standard
        };

        ImageAspectRatio aspectRatio{
            ImageAspectRatio::Auto
        };

        std::optional<std::int64_t> seed;

        // Backward-compatible developer overrides.
        //
        // These are intentionally NOT advertised by GenerateImageRegisteredTool.
        // They keep older diagnostic ToolRequests usable while Rose transitions
        // to intent-level image generation.
        std::optional<int> widthOverride;
        std::optional<int> heightOverride;
        std::optional<int> stepsOverride;
        std::optional<float> cfgScaleOverride;
    };


    struct ImageGenerationProfile
    {
        std::string name;

        // Square long-edge/base size. Other aspects derive from this value.
        int baseSize{ 512 };

        int steps{ 20 };
        float cfgScale{ 7.0f };
    };


    struct ImageGenerationProfileSet
    {
        // Optional provider/model-specific conditioning prepended after Rose has
        // already captured the user's original request. This is for model-native
        // prompt conventions (for example Pony score tags), not policy filtering.
        std::string promptPrefix;

        ImageGenerationProfile draft;
        ImageGenerationProfile standard;
        ImageGenerationProfile high;
    };


    struct ResolvedImageGeneration
    {
        ImageGenerationRequest request;

        ImageGenerationQuality quality{
            ImageGenerationQuality::Standard
        };

        ImageAspectRatio aspectRatio{
            ImageAspectRatio::Square
        };

        std::string profileName;
    };


    [[nodiscard]]
    ImageGenerationProfileSet makeStableDiffusion15Profiles();


    // FLUX.2 [klein] 4B distilled uses fixed four-step/guidance-distilled
    // inference. Quality levels therefore primarily adjust requested resolution,
    // not sampling depth.
    [[nodiscard]]
    ImageGenerationProfileSet makeFlux2Klein4BProfiles();


    // SDXL-family checkpoint profiles. These are deliberately model-specific
    // starting points rather than a single universal SDXL tune so Rose can
    // evolve each preset independently as local testing produces better data.
    [[nodiscard]]
    ImageGenerationProfileSet makeRealVisXLV5Profiles();


    [[nodiscard]]
    ImageGenerationProfileSet makeJuggernautXLProfiles();


    [[nodiscard]]
    ImageGenerationProfileSet makePonyV6Profiles();


    [[nodiscard]]
    ResolvedImageGeneration resolveImageGenerationIntent(
        const ImageGenerationIntent& intent,
        const ImageGenerationProfileSet& profiles);


    [[nodiscard]]
    ImageGenerationQuality parseImageGenerationQuality(
        std::string_view value);


    [[nodiscard]]
    ImageAspectRatio parseImageAspectRatio(
        std::string_view value);


    [[nodiscard]]
    std::string_view toString(
        ImageGenerationQuality quality) noexcept;


    [[nodiscard]]
    std::string_view toString(
        ImageAspectRatio aspectRatio) noexcept;

} // namespace rose::imagegen
