#include "artifacts/ArtifactStore.h"
#include "imagegen/StableDiffusionCliGenerator.h"
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

        rose::tools::GenerateImageTool tool{
            generator,
            store
        };

        rose::imagegen::ImageGenerationRequest request;
        request.prompt = argv[2];
        request.width = 512;
        request.height = 512;
        request.steps = 20;
        request.cfgScale = 7.0f;

        const rose::artifacts::Artifact artifact =
            tool.generate(request);

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
