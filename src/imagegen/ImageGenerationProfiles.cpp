#include "imagegen/ImageGenerationProfiles.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace rose::imagegen
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
                result.push_back(
                    static_cast<char>(
                        std::tolower(character)));
            }

            return result;
        }


        [[nodiscard]]
        bool containsWord(
            const std::string& lowerText,
            const std::string_view word)
        {
            std::size_t position{
                0
            };

            while (true)
            {
                position =
                    lowerText.find(
                        word,
                        position);

                if (position == std::string::npos)
                {
                    return false;
                }

                const bool leftBoundary =
                    position == 0
                    || std::isalnum(
                        static_cast<unsigned char>(
                            lowerText[position - 1]))
                        == 0;

                const std::size_t end =
                    position + word.size();

                const bool rightBoundary =
                    end >= lowerText.size()
                    || std::isalnum(
                        static_cast<unsigned char>(
                            lowerText[end]))
                        == 0;

                if (
                    leftBoundary
                    && rightBoundary)
                {
                    return true;
                }

                position =
                    end;
            }
        }


        [[nodiscard]]
        ImageAspectRatio inferAspectRatio(
            const ImageGenerationIntent& intent)
        {
            if (
                intent.aspectRatio
                != ImageAspectRatio::Auto)
            {
                return intent.aspectRatio;
            }

            const std::string lowerPrompt =
                asciiLower(
                    intent.prompt);

            if (
                containsWord(lowerPrompt, "ultrawide")
                || containsWord(lowerPrompt, "widescreen")
                || containsWord(lowerPrompt, "panorama")
                || containsWord(lowerPrompt, "panoramic"))
            {
                return ImageAspectRatio::Wide;
            }

            if (
                containsWord(lowerPrompt, "portrait")
                || containsWord(lowerPrompt, "vertical"))
            {
                return ImageAspectRatio::Portrait;
            }

            if (
                containsWord(lowerPrompt, "landscape")
                || containsWord(lowerPrompt, "horizontal")
                || containsWord(lowerPrompt, "wallpaper"))
            {
                return ImageAspectRatio::Landscape;
            }

            return ImageAspectRatio::Square;
        }


        [[nodiscard]]
        const ImageGenerationProfile& selectProfile(
            const ImageGenerationQuality quality,
            const ImageGenerationProfileSet& profiles)
        {
            switch (quality)
            {
            case ImageGenerationQuality::Draft:
                return profiles.draft;

            case ImageGenerationQuality::Standard:
                return profiles.standard;

            case ImageGenerationQuality::High:
                return profiles.high;
            }

            throw std::invalid_argument{
                "Unknown image-generation quality."
            };
        }


        [[nodiscard]]
        int floorToMultipleOf64(
            const int value)
        {
            const int clamped =
                std::max(
                    64,
                    value);

            return
                std::max(
                    64,
                    (clamped / 64)
                    * 64);
        }


        void resolveDimensions(
            const int baseSize,
            const ImageAspectRatio aspectRatio,
            int& width,
            int& height)
        {
            const int safeBase =
                floorToMultipleOf64(
                    baseSize);

            switch (aspectRatio)
            {
            case ImageAspectRatio::Square:
            case ImageAspectRatio::Auto:
                width = safeBase;
                height = safeBase;
                break;

            case ImageAspectRatio::Portrait:
                width =
                    floorToMultipleOf64(
                        (safeBase * 3) / 4);

                height =
                    safeBase;

                break;

            case ImageAspectRatio::Landscape:
                width =
                    safeBase;

                height =
                    floorToMultipleOf64(
                        (safeBase * 3) / 4);

                break;

            case ImageAspectRatio::Wide:
                width =
                    safeBase;

                height =
                    floorToMultipleOf64(
                        (safeBase * 9) / 16);

                break;
            }
        }


        void validateResolvedRequest(
            const ImageGenerationRequest& request)
        {
            if (request.prompt.empty())
            {
                throw std::invalid_argument{
                    "Image prompt must not be empty."
                };
            }

            if (
                request.width <= 0
                || request.height <= 0)
            {
                throw std::invalid_argument{
                    "Resolved image dimensions must be greater than zero."
                };
            }

            if (
                request.steps <= 0
                || request.steps > 250)
            {
                throw std::invalid_argument{
                    "Resolved image-generation steps must be within 1..250."
                };
            }

            if (
                request.cfgScale < 0.0f
                || request.cfgScale > 50.0f)
            {
                throw std::invalid_argument{
                    "Resolved image CFG scale must be within 0..50."
                };
            }
        }
    }


    ImageGenerationProfileSet makeStableDiffusion15Profiles()
    {
        // SD1.5 is primarily a 512px model. High quality therefore increases
        // sampling effort and only nudges base size upward rather than pretending
        // this older model is a native 1024px generator.
        return ImageGenerationProfileSet{
            .promptPrefix = {},
            .draft = ImageGenerationProfile{
                .name = "sd15-draft",
                .baseSize = 512,
                .steps = 12,
                .cfgScale = 7.0f
            },
            .standard = ImageGenerationProfile{
                .name = "sd15-standard",
                .baseSize = 512,
                .steps = 24,
                .cfgScale = 7.0f
            },
            .high = ImageGenerationProfile{
                .name = "sd15-high",
                .baseSize = 640,
                .steps = 32,
                .cfgScale = 7.0f
            }
        };
    }


    ImageGenerationProfileSet makeFlux2Klein4BProfiles()
    {
        // FLUX.2 [klein] 4B distilled is guidance- and step-distilled. The
        // upstream defaults are four steps with guidance/CFG 1.0, so Rose does
        // not waste time pretending "high quality" means more sampling steps.
        //
        // Instead the semantic quality levels control output resolution.
        return ImageGenerationProfileSet{
            .promptPrefix = {},
            .draft = ImageGenerationProfile{
                .name = "flux2-klein-4b-draft",
                .baseSize = 768,
                .steps = 4,
                .cfgScale = 1.0f
            },
            .standard = ImageGenerationProfile{
                .name = "flux2-klein-4b-standard",
                .baseSize = 1024,
                .steps = 4,
                .cfgScale = 1.0f
            },
            .high = ImageGenerationProfile{
                .name = "flux2-klein-4b-high",
                .baseSize = 1280,
                .steps = 4,
                .cfgScale = 1.0f
            }
        };
    }


    ImageGenerationProfileSet makeRealVisXLV5Profiles()
    {
        // RealVisXL is an SDXL-family photoreal checkpoint. Keep draft below
        // native 1024 for responsiveness, use native 1024 for the normal path,
        // and spend high-quality budget on both resolution and sampling depth.
        return ImageGenerationProfileSet{
            .promptPrefix = {},
            .draft = ImageGenerationProfile{
                .name = "realvisxl-v5-draft",
                .baseSize = 896,
                .steps = 20,
                .cfgScale = 5.0f
            },
            .standard = ImageGenerationProfile{
                .name = "realvisxl-v5-standard",
                .baseSize = 1024,
                .steps = 28,
                .cfgScale = 5.0f
            },
            .high = ImageGenerationProfile{
                .name = "realvisxl-v5-high",
                .baseSize = 1216,
                .steps = 35,
                .cfgScale = 5.0f
            }
        };
    }


    ImageGenerationProfileSet makeJuggernautXLProfiles()
    {
        // Juggernaut XL is also SDXL-family and photoreal-oriented. Start a
        // little higher on sampling/guidance than RealVisXL while keeping the
        // same resolution tiers so A/B comparisons remain straightforward.
        return ImageGenerationProfileSet{
            .promptPrefix = {},
            .draft = ImageGenerationProfile{
                .name = "juggernaut-xl-draft",
                .baseSize = 896,
                .steps = 22,
                .cfgScale = 5.5f
            },
            .standard = ImageGenerationProfile{
                .name = "juggernaut-xl-standard",
                .baseSize = 1024,
                .steps = 30,
                .cfgScale = 5.5f
            },
            .high = ImageGenerationProfile{
                .name = "juggernaut-xl-high",
                .baseSize = 1216,
                .steps = 38,
                .cfgScale = 5.5f
            }
        };
    }


    ImageGenerationProfileSet makePonyV6Profiles()
    {
        // Pony V6 is SDXL-family but is commonly used for stylized/anime/fantasy
        // work. Preserve native 1024 even in draft mode and use somewhat stronger
        // guidance than the photoreal presets as a conservative starting tune.
        return ImageGenerationProfileSet{
            // Pony V6 was trained around these score tags. Keeping them in the
            // model profile lets callers continue using normal natural language.
            .promptPrefix =
                "score_9, score_8_up, score_7_up, score_6_up, "
                "score_5_up, score_4_up, ",
            .draft = ImageGenerationProfile{
                .name = "pony-v6-draft",
                .baseSize = 1024,
                .steps = 24,
                .cfgScale = 6.0f
            },
            .standard = ImageGenerationProfile{
                .name = "pony-v6-standard",
                .baseSize = 1024,
                .steps = 30,
                .cfgScale = 6.5f
            },
            .high = ImageGenerationProfile{
                .name = "pony-v6-high",
                .baseSize = 1216,
                .steps = 36,
                .cfgScale = 6.5f
            }
        };
    }


    ResolvedImageGeneration resolveImageGenerationIntent(
        const ImageGenerationIntent& intent,
        const ImageGenerationProfileSet& profiles)
    {
        const ImageGenerationProfile& profile =
            selectProfile(
                intent.quality,
                profiles);

        const ImageAspectRatio resolvedAspect =
            inferAspectRatio(
                intent);

        ImageGenerationRequest request;

        // Keep model-specific prompt conventions in the profile layer rather
        // than teaching the agent about checkpoint quirks. The original user
        // prompt remains available in the ToolRequest/agent journal, while the
        // resolved provider-facing request records the effective prompt.
        request.prompt =
            profiles.promptPrefix;
        request.prompt +=
            intent.prompt;

        request.negativePrompt =
            intent.negativePrompt;

        request.steps =
            profile.steps;

        request.cfgScale =
            profile.cfgScale;

        request.seed =
            intent.seed;

        resolveDimensions(
            profile.baseSize,
            resolvedAspect,
            request.width,
            request.height);

        // Compatibility bridge for older development requests/checkpoints.
        // Agent-facing descriptors no longer advertise these values.
        if (intent.widthOverride.has_value())
        {
            request.width =
                *intent.widthOverride;
        }

        if (intent.heightOverride.has_value())
        {
            request.height =
                *intent.heightOverride;
        }

        if (intent.stepsOverride.has_value())
        {
            request.steps =
                *intent.stepsOverride;
        }

        if (intent.cfgScaleOverride.has_value())
        {
            request.cfgScale =
                *intent.cfgScaleOverride;
        }

        validateResolvedRequest(
            request);

        return ResolvedImageGeneration{
            .request =
                std::move(request),
            .quality =
                intent.quality,
            .aspectRatio =
                resolvedAspect,
            .profileName =
                profile.name
        };
    }


    ImageGenerationQuality parseImageGenerationQuality(
        const std::string_view value)
    {
        if (
            value.empty()
            || value == "standard")
        {
            return ImageGenerationQuality::Standard;
        }

        if (value == "draft")
        {
            return ImageGenerationQuality::Draft;
        }

        if (
            value == "high"
            || value == "high_quality")
        {
            return ImageGenerationQuality::High;
        }

        throw std::invalid_argument{
            "Image quality must be draft, standard, or high."
        };
    }


    ImageAspectRatio parseImageAspectRatio(
        const std::string_view value)
    {
        if (
            value.empty()
            || value == "auto")
        {
            return ImageAspectRatio::Auto;
        }

        if (value == "square")
        {
            return ImageAspectRatio::Square;
        }

        if (value == "portrait")
        {
            return ImageAspectRatio::Portrait;
        }

        if (value == "landscape")
        {
            return ImageAspectRatio::Landscape;
        }

        if (value == "wide")
        {
            return ImageAspectRatio::Wide;
        }

        throw std::invalid_argument{
            "Image aspect_ratio must be auto, square, portrait, landscape, or wide."
        };
    }


    std::string_view toString(
        const ImageGenerationQuality quality) noexcept
    {
        switch (quality)
        {
        case ImageGenerationQuality::Draft:
            return "draft";

        case ImageGenerationQuality::Standard:
            return "standard";

        case ImageGenerationQuality::High:
            return "high";
        }

        return "unknown";
    }


    std::string_view toString(
        const ImageAspectRatio aspectRatio) noexcept
    {
        switch (aspectRatio)
        {
        case ImageAspectRatio::Auto:
            return "auto";

        case ImageAspectRatio::Square:
            return "square";

        case ImageAspectRatio::Portrait:
            return "portrait";

        case ImageAspectRatio::Landscape:
            return "landscape";

        case ImageAspectRatio::Wide:
            return "wide";
        }

        return "unknown";
    }

} // namespace rose::imagegen
