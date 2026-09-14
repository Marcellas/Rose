#pragma once

#include "artifacts/Artifact.h"
#include "artifacts/ArtifactStore.h"
#include "imagegen/IImageGenerator.h"

namespace rose::tools
{

    // Tool-layer bridge between an image generator and Rose-owned artifact storage.
    //
    // Ownership:
    // - borrows IImageGenerator
    // - borrows ArtifactStore
    // - returns an Artifact value whose backing file persists in the store
    class GenerateImageTool final
    {
    public:
        GenerateImageTool(
            imagegen::IImageGenerator& generator,
            artifacts::ArtifactStore& artifactStore);

        [[nodiscard]]
        artifacts::Artifact generate(
            const imagegen::ImageGenerationRequest& request);

    private:
        imagegen::IImageGenerator& generator_;
        artifacts::ArtifactStore& artifactStore_;
    };

} // namespace rose::tools
