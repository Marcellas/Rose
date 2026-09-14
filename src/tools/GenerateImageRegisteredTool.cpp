#include "tools/GenerateImageRegisteredTool.h"

#include "imagegen/ImageGenerationTypes.h"

#include <charconv>
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
            .parameters = {
                ToolParameterDescriptor{
                    .name = "prompt",
                    .description = "Description of the image to generate.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "width",
                    .description = "Output width in pixels. Default 512.",
                    .type = ToolValueType::Integer,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "height",
                    .description = "Output height in pixels. Default 512.",
                    .type = ToolValueType::Integer,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "steps",
                    .description = "Diffusion sampling steps. Default 20.",
                    .type = ToolValueType::Integer,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "cfg_scale",
                    .description = "Classifier-free guidance scale. Default 7.0.",
                    .type = ToolValueType::Number,
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

        imagegen::ImageGenerationRequest imageRequest;
        imageRequest.prompt =
            requiredArgument(
                request,
                "prompt");

        imageRequest.width =
            parseOptionalInt(
                request,
                "width",
                512);

        imageRequest.height =
            parseOptionalInt(
                request,
                "height",
                512);

        imageRequest.steps =
            parseOptionalInt(
                request,
                "steps",
                20);

        imageRequest.cfgScale =
            parseOptionalFloat(
                request,
                "cfg_scale",
                7.0f);

        if (
            imageRequest.width <= 0
            || imageRequest.height <= 0)
        {
            throw std::invalid_argument{
                "Image width and height must be greater than zero."
            };
        }

        if (imageRequest.steps <= 0)
        {
            throw std::invalid_argument{
                "Image generation steps must be greater than zero."
            };
        }

        if (imageRequest.cfgScale <= 0.0f)
        {
            throw std::invalid_argument{
                "Image generation cfg_scale must be greater than zero."
            };
        }

        artifacts::Artifact artifact =
            generateImageTool_.generate(
                imageRequest);

        ToolResult result;
        result.success = true;
        result.message =
            "Generated the image locally with stable-diffusion.cpp.";
        result.artifacts.push_back(
            std::move(artifact));

        return result;
    }

} // namespace rose::tools
