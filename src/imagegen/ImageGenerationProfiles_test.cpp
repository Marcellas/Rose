#include "imagegen/ImageGenerationProfiles.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    void require(
        const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            std::cerr
                << "ImageGenerationProfiles test failed: "
                << message
                << '\n';

            std::exit(1);
        }
    }
}


int main()
{
    using namespace rose::imagegen;

    const ImageGenerationProfileSet profiles =
        makeStableDiffusion15Profiles();

    {
        ImageGenerationIntent intent;
        intent.prompt = "A red lighthouse.";

        const ResolvedImageGeneration resolved =
            resolveImageGenerationIntent(
                intent,
                profiles);

        require(
            resolved.quality
                == ImageGenerationQuality::Standard,
            "default quality should be Standard");

        require(
            resolved.aspectRatio
                == ImageAspectRatio::Square,
            "auto aspect should default to Square");

        require(
            resolved.request.width == 512
                && resolved.request.height == 512,
            "SD1.5 standard square should remain 512x512");

        require(
            resolved.request.steps == 24,
            "standard profile should use 24 steps");
    }


    {
        ImageGenerationIntent intent;
        intent.prompt =
            "Quick draft concept sketch of an adult dwarf blacksmith.";

        intent.quality =
            ImageGenerationQuality::Draft;

        intent.aspectRatio =
            ImageAspectRatio::Portrait;

        const ResolvedImageGeneration resolved =
            resolveImageGenerationIntent(
                intent,
                profiles);

        require(
            resolved.request.width == 384
                && resolved.request.height == 512,
            "portrait draft should resolve to 384x512");

        require(
            resolved.request.steps == 12,
            "draft profile should use fewer steps");
    }


    {
        ImageGenerationIntent intent;
        intent.prompt =
            "High-detail panoramic fantasy city concept art.";

        intent.quality =
            ImageGenerationQuality::High;

        const ResolvedImageGeneration resolved =
            resolveImageGenerationIntent(
                intent,
                profiles);

        require(
            resolved.aspectRatio
                == ImageAspectRatio::Wide,
            "panoramic prompt should infer Wide when aspect is Auto");

        require(
            resolved.request.width == 640
                && resolved.request.height == 320,
            "high wide profile should resolve to 640x320");

        require(
            resolved.request.steps == 32,
            "high profile should use 32 steps");
    }


    {
        ImageGenerationIntent intent;
        intent.prompt =
            "Vertical anatomical turnaround of an adult pixie.";

        const ResolvedImageGeneration resolved =
            resolveImageGenerationIntent(
                intent,
                profiles);

        require(
            resolved.aspectRatio
                == ImageAspectRatio::Portrait,
            "vertical prompt should infer Portrait");
    }


    {
        ImageGenerationIntent intent;
        intent.prompt =
            "Legacy diagnostic image.";

        intent.widthOverride = 704;
        intent.heightOverride = 512;
        intent.stepsOverride = 18;
        intent.cfgScaleOverride = 6.5f;

        const ResolvedImageGeneration resolved =
            resolveImageGenerationIntent(
                intent,
                profiles);

        require(
            resolved.request.width == 704
                && resolved.request.height == 512,
            "legacy dimension overrides should remain supported");

        require(
            resolved.request.steps == 18,
            "legacy steps override should remain supported");

        require(
            resolved.request.cfgScale == 6.5f,
            "legacy CFG override should remain supported");
    }


    {
        const ImageGenerationProfileSet fluxProfiles =
            makeFlux2Klein4BProfiles();

        ImageGenerationIntent intent;
        intent.prompt =
            "High quality landscape concept art.";

        intent.quality =
            ImageGenerationQuality::High;

        intent.aspectRatio =
            ImageAspectRatio::Landscape;

        const ResolvedImageGeneration resolved =
            resolveImageGenerationIntent(
                intent,
                fluxProfiles);

        require(
            resolved.request.width == 1280
                && resolved.request.height == 960,
            "FLUX.2 high landscape should resolve to 1280x960");

        require(
            resolved.request.steps == 4,
            "FLUX.2 Klein distilled should remain four-step");

        require(
            resolved.request.cfgScale == 1.0f,
            "FLUX.2 Klein distilled should use CFG 1.0");
    }


    {
        const ImageGenerationProfileSet realVisProfiles =
            makeRealVisXLV5Profiles();

        ImageGenerationIntent intent;
        intent.prompt = "Photoreal studio image.";

        const ResolvedImageGeneration resolved =
            resolveImageGenerationIntent(
                intent,
                realVisProfiles);

        require(
            resolved.request.width == 1024
                && resolved.request.height == 1024,
            "RealVisXL standard square should resolve to 1024x1024");

        require(
            resolved.request.steps == 28
                && resolved.request.cfgScale == 5.0f,
            "RealVisXL standard tuning should remain model-specific");
    }


    {
        const ImageGenerationProfileSet juggernautProfiles =
            makeJuggernautXLProfiles();

        ImageGenerationIntent intent;
        intent.prompt = "Cinematic horizontal portrait.";
        intent.quality = ImageGenerationQuality::High;
        intent.aspectRatio = ImageAspectRatio::Landscape;

        const ResolvedImageGeneration resolved =
            resolveImageGenerationIntent(
                intent,
                juggernautProfiles);

        require(
            resolved.request.width == 1216
                && resolved.request.height == 896,
            "Juggernaut high landscape should resolve to 1216x896");

        require(
            resolved.request.steps == 38
                && resolved.request.cfgScale == 5.5f,
            "Juggernaut high tuning should remain model-specific");
    }


    {
        const ImageGenerationProfileSet ponyProfiles =
            makePonyV6Profiles();

        ImageGenerationIntent intent;
        intent.prompt = "Anime fantasy character portrait.";
        intent.quality = ImageGenerationQuality::Draft;
        intent.aspectRatio = ImageAspectRatio::Portrait;

        const ResolvedImageGeneration resolved =
            resolveImageGenerationIntent(
                intent,
                ponyProfiles);

        require(
            resolved.request.width == 768
                && resolved.request.height == 1024,
            "Pony draft portrait should preserve native 1024 base size");

        require(
            resolved.request.steps == 24
                && resolved.request.cfgScale == 6.0f,
            "Pony draft tuning should remain model-specific");
    }


    {
        const rose::imagegen::ImageGenerationProfileSet pony =
            rose::imagegen::makePonyV6Profiles();

        rose::imagegen::ImageGenerationIntent intent;
        intent.prompt = "adult red-haired pixie woman";

        const rose::imagegen::ResolvedImageGeneration resolved =
            rose::imagegen::resolveImageGenerationIntent(
                intent,
                pony);

        require(
            resolved.request.prompt.starts_with(
                "score_9, score_8_up, score_7_up, score_6_up, "
                "score_5_up, score_4_up, "),
            "Pony V6 should prepend its model-native score-tag conditioning");

        require(
            resolved.request.prompt.ends_with(
                "adult red-haired pixie woman"),
            "Pony conditioning must preserve the original prompt text");
    }


    std::cout
        << "Rose ImageGenerationProfiles tests: PASS\n";

    return 0;
}
