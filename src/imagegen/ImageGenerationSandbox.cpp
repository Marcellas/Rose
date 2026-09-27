#include "artifacts/ArtifactStore.h"
#include "imagegen/ImageGenerationProfiles.h"
#include "imagegen/StableDiffusionCliGenerator.h"
#include "policy/ContentPolicy.h"
#include "tools/GenerateImageTool.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

int main(
    const int argc,
    char** argv)
{
    try
    {
        if (argc < 3)
        {
            std::cout
                << "Usage: RoseImageGenSandbox <model-file> <prompt>\n";
            return 0;
        }

        rose::imagegen::StableDiffusionCliConfig config;
        config.modelPath = argv[1];
        config.maxVramAssignment = "cuda0=8";

        rose::imagegen::StableDiffusionCliGenerator generator{
            std::move(config)
        };

        std::cout
            << generator.availabilityMessage()
            << '\n';

        rose::artifacts::ArtifactStore store{
            "data/artifacts/sandbox"
        };

        rose::policy::ContentPolicy contentPolicy{
            rose::policy::ContentPolicyConfig{
                .mode =
                    rose::policy::ContentMode::
                        DevelopmentUnrestricted
            }
        };

        rose::tools::GenerateImageTool tool{
            generator,
            store,
            contentPolicy,
            rose::imagegen::makeStableDiffusion15Profiles()
        };

        rose::imagegen::ImageGenerationIntent intent;
        intent.prompt = argv[2];
        intent.quality =
            rose::imagegen::ImageGenerationQuality::Standard;
        intent.aspectRatio =
            rose::imagegen::ImageAspectRatio::Auto;

        const rose::tools::GeneratedImageOutcome generated =
            tool.generate(intent);

        const rose::artifacts::Artifact& artifact =
            generated.artifact;

        std::cout
            << "Generated artifact:\n"
            << artifact.path.string()
            << '\n';

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Image generation sandbox error: "
            << exception.what()
            << '\n';

        return 1;
    }
}
