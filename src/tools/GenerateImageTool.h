#pragma once

#include "artifacts/Artifact.h"
#include "artifacts/ArtifactStore.h"
#include "imagegen/IImageGenerator.h"
#include "imagegen/ImageGenerationProfiles.h"
#include "policy/ContentPolicy.h"

namespace rose::tools
{
    struct GeneratedImageOutcome
    {
        artifacts::Artifact artifact;
        std::string profileName;
        int width{ 0 };
        int height{ 0 };
    };


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
            artifacts::ArtifactStore& artifactStore,
            policy::ContentPolicy& contentPolicy,
            imagegen::ImageGenerationProfileSet profiles);

        [[nodiscard]]
        GeneratedImageOutcome generate(
            const imagegen::ImageGenerationIntent& intent,
            policy::SubjectLifeStage declaredLifeStage =
                policy::SubjectLifeStage::Unknown);

    private:
        imagegen::IImageGenerator& generator_;
        artifacts::ArtifactStore& artifactStore_;
        policy::ContentPolicy& contentPolicy_;

        // Provider/model-specific tuning lives here rather than in Agent prompts.
        // GenerateImageTool owns this small immutable value set.
        imagegen::ImageGenerationProfileSet profiles_;
    };

} // namespace rose::tools
