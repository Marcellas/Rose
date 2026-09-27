#include "tools/GenerateImageRegisteredTool.h"

#include "imagegen/ImageGenerationProfiles.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]]
        const std::string& requiredArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

            if (
                found == request.arguments.end()
                || found->second.empty())
            {
                throw std::invalid_argument{
                    "Tool '"
                    + request.toolId
                    + "' requires argument '"
                    + std::string{ name }
                    + "'."
                };
            }

            return found->second;
        }


        [[nodiscard]]
        int parseOptionalInt(
            const ToolRequest& request,
            const std::string_view name,
            const int fallback)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

            if (found == request.arguments.end())
            {
                return fallback;
            }

            int value{};
            const std::string& text = found->second;

            const auto [end, error] =
                std::from_chars(
                    text.data(),
                    text.data() + text.size(),
                    value);

            if (
                error != std::errc{}
                || end != text.data() + text.size())
            {
                throw std::invalid_argument{
                    "Tool argument '"
                    + std::string{ name }
                    + "' must be an integer."
                };
            }

            return value;
        }


        [[nodiscard]]
        std::optional<std::int64_t> parseOptionalInt64(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

            if (found == request.arguments.end())
            {
                return std::nullopt;
            }

            std::int64_t value{};
            const std::string& text = found->second;

            const auto [end, error] =
                std::from_chars(
                    text.data(),
                    text.data() + text.size(),
                    value);

            if (
                error != std::errc{}
                || end != text.data() + text.size())
            {
                throw std::invalid_argument{
                    "Tool argument '"
                    + std::string{ name }
                    + "' must be an integer."
                };
            }

            return value;
        }


        [[nodiscard]]
        policy::SubjectLifeStage parseOptionalSubjectMaturity(
            const ToolRequest& request)
        {
            const auto found =
                request.arguments.find(
                    "subject_maturity");

            if (found == request.arguments.end())
            {
                return policy::SubjectLifeStage::Unknown;
            }

            const std::string& value =
                found->second;

            if (value == "adult")
            {
                return policy::SubjectLifeStage::Adult;
            }

            if (value == "juvenile")
            {
                return policy::SubjectLifeStage::Juvenile;
            }

            if (
                value.empty()
                || value == "unknown")
            {
                return policy::SubjectLifeStage::Unknown;
            }

            throw std::invalid_argument{
                "Tool argument 'subject_maturity' must be adult, juvenile, or unknown."
            };
        }


        [[nodiscard]]
        float parseOptionalFloat(
            const ToolRequest& request,
            const std::string_view name,
            const float fallback)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

            if (found == request.arguments.end())
            {
                return fallback;
            }

            try
            {
                std::size_t consumed{};
                const float value =
                    std::stof(
                        found->second,
                        &consumed);

                if (consumed != found->second.size())
                {
                    throw std::invalid_argument{ "trailing data" };
                }

                return value;
            }
            catch (const std::exception&)
            {
                throw std::invalid_argument{
                    "Tool argument '"
                    + std::string{ name }
                    + "' must be a number."
                };
            }
        }
    }


    GenerateImageRegisteredTool::GenerateImageRegisteredTool(
        GenerateImageTool& generateImageTool)
        : generateImageTool_{ generateImageTool }
        , descriptor_{
            .id = "generate_image",
            .displayName = "Generate Image",
            .description =
                "Generate a new local image from a text prompt and store it "
                "as a Rose-owned artifact.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::AutoAllowed,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "prompt",
                    .description = "Description of the image to generate.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "quality",
                    .description =
                        "Intent-level render effort/resolution: draft, standard, "
                        "or high. Default standard. Omit unless the user expresses "
                        "a quality/detail/resolution/speed preference. Content words "
                        "such as explicit, nude, mature, or adult do not imply high. "
                        "Do not invent diffusion steps/CFG.",
                    .type = ToolValueType::String,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "aspect_ratio",
                    .description =
                        "Composition: auto, square, portrait, landscape, or wide. "
                        "Use auto when the user's composition intent is unclear.",
                    .type = ToolValueType::String,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "seed",
                    .description =
                        "Optional deterministic image seed. Omit unless the user "
                        "requests repeatability or a specific seed.",
                    .type = ToolValueType::Integer,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "subject_maturity",
                    .description =
                        "Optional maturity context for depicted subjects: adult, "
                        "juvenile, or unknown. This means life stage for the "
                        "species; height/stature does not determine maturity.",
                    .type = ToolValueType::String,
                    .required = false
                }
            }
        }
    {
    }


    const ToolDescriptor& GenerateImageRegisteredTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult GenerateImageRegisteredTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "GenerateImageRegisteredTool received a request for a different tool."
            };
        }

        imagegen::ImageGenerationIntent imageIntent;
        imageIntent.prompt =
            requiredArgument(
                request,
                "prompt");

        const auto qualityFound =
            request.arguments.find(
                "quality");

        imageIntent.quality =
            imagegen::parseImageGenerationQuality(
                qualityFound == request.arguments.end()
                    ? std::string_view{}
                    : std::string_view{
                        qualityFound->second
                    });

        const auto aspectFound =
            request.arguments.find(
                "aspect_ratio");

        imageIntent.aspectRatio =
            imagegen::parseImageAspectRatio(
                aspectFound == request.arguments.end()
                    ? std::string_view{}
                    : std::string_view{
                        aspectFound->second
                    });

        imageIntent.seed =
            parseOptionalInt64(
                request,
                "seed");

        // Compatibility bridge for older development requests/checkpoints.
        // These arguments are intentionally absent from the descriptor, so the
        // Agent is no longer encouraged to reason about diffusion internals.
        if (request.arguments.contains("width"))
        {
            imageIntent.widthOverride =
                parseOptionalInt(
                    request,
                    "width",
                    512);
        }

        if (request.arguments.contains("height"))
        {
            imageIntent.heightOverride =
                parseOptionalInt(
                    request,
                    "height",
                    512);
        }

        if (request.arguments.contains("steps"))
        {
            imageIntent.stepsOverride =
                parseOptionalInt(
                    request,
                    "steps",
                    20);
        }

        if (request.arguments.contains("cfg_scale"))
        {
            imageIntent.cfgScaleOverride =
                parseOptionalFloat(
                    request,
                    "cfg_scale",
                    7.0f);
        }

        const policy::SubjectLifeStage subjectMaturity =
            parseOptionalSubjectMaturity(
                request);

        GeneratedImageOutcome generated =
            generateImageTool_.generate(
                imageIntent,
                subjectMaturity);

        ToolResult result;
        result.success = true;
        result.message =
            "Generated the image successfully with profile '"
            + generated.profileName
            + "' at "
            + std::to_string(generated.width)
            + "x"
            + std::to_string(generated.height)
            + ".";

        result.responseMode =
            ToolResponseMode::AuthoritativeCompletion;

        result.artifacts.push_back(
            std::move(generated.artifact));

        return result;
    }

} // namespace rose::tools
