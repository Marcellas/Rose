#include "tools/GenerateImageTool.h"

#include <fstream>
#include <stdexcept>
#include <string>

namespace rose::tools
{

    GenerateImageTool::GenerateImageTool(
        imagegen::IImageGenerator& generator,
        artifacts::ArtifactStore& artifactStore)
        : generator_{ generator }
        , artifactStore_{ artifactStore }
    {
    }


    artifacts::Artifact GenerateImageTool::generate(
        const imagegen::ImageGenerationRequest& request)
    {
        if (!generator_.available())
        {
            throw std::runtime_error{
                generator_.availabilityMessage()
            };
        }

        const std::filesystem::path outputPath =
            artifactStore_.allocatePath(
                "generated-image",
                ".png");

        const imagegen::ImageGenerationResult result =
            generator_.generate(
                request,
                outputPath);

        artifacts::Artifact artifact =
            artifactStore_.finalize(
                result.outputPath,
                result.outputPath.filename().string(),
                "image/png",
                artifacts::ArtifactKind::Image);

        // Keep generation provenance local beside the image. This is deliberately a
        // sidecar rather than PNG-specific metadata so the Artifact layer remains
        // format-neutral.
        const std::filesystem::path metadataPath =
            result.outputPath.string()
            + ".rosemeta.txt";

        std::ofstream metadata{
            metadataPath,
            std::ios::binary | std::ios::trunc
        };

        if (metadata)
        {
            metadata
                << "provider="
                << result.providerName
                << '\n'
                << "width="
                << request.width
                << '\n'
                << "height="
                << request.height
                << '\n'
                << "steps="
                << request.steps
                << '\n'
                << "cfg_scale="
                << request.cfgScale
                << '\n';

            if (result.seed.has_value())
            {
                metadata
                    << "seed="
                    << *result.seed
                    << '\n';
            }

            metadata
                << "prompt_begin\n"
                << request.prompt
                << "\nprompt_end\n";

            if (!request.negativePrompt.empty())
            {
                metadata
                    << "negative_prompt_begin\n"
                    << request.negativePrompt
                    << "\nnegative_prompt_end\n";
            }

            metadata.flush();

            if (metadata)
            {
                artifact.metadataPath =
                    metadataPath;
            }
        }

        return artifact;
    }

} // namespace rose::tools
