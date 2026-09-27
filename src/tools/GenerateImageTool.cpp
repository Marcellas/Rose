#include "tools/GenerateImageTool.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace rose::tools
{

    GenerateImageTool::GenerateImageTool(
        imagegen::IImageGenerator& generator,
        artifacts::ArtifactStore& artifactStore,
        policy::ContentPolicy& contentPolicy,
        imagegen::ImageGenerationProfileSet profiles)
        : generator_{ generator }
        , artifactStore_{ artifactStore }
        , contentPolicy_{ contentPolicy }
        , profiles_{ std::move(profiles) }
    {
    }


    GeneratedImageOutcome GenerateImageTool::generate(
        const imagegen::ImageGenerationIntent& intent,
        const policy::SubjectLifeStage declaredLifeStage)
    {
        // Content policy is enforced BEFORE availability/provider dispatch.
        // This guarantees that swapping SD1.5 for FLUX/Qwen/another backend does
        // not silently change Rose's configured content boundary.
        const policy::ContentPolicyDecision policyDecision =
            contentPolicy_.evaluateImagePrompt(
                intent.prompt,
                declaredLifeStage);

        if (!policyDecision.allowed())
        {
            throw std::runtime_error{
                policyDecision.reason
            };
        }

        const imagegen::ResolvedImageGeneration resolved =
            imagegen::resolveImageGenerationIntent(
                intent,
                profiles_);

        const imagegen::ImageGenerationRequest& request =
            resolved.request;

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
                << '\n';

            if (!result.diagnosticLogPath.empty())
            {
                metadata
                    << "diagnostic_log_path="
                    << result.diagnosticLogPath.string()
                    << '\n';
            }

            for (const imagegen::ImageGenerationProvenanceEntry& entry :
                 result.provenance)
            {
                metadata
                    << entry.key
                    << '='
                    << entry.value
                    << '\n';
            }

            metadata
                << "content_mode="
                << policy::ContentPolicy::toString(
                    contentPolicy_.mode())
                << '\n'
                << "content_level="
                << policy::ContentPolicy::toString(
                    policyDecision.contentLevel)
                << '\n'
                << "resolved_subject_maturity="
                << policy::ContentPolicy::toString(
                    policyDecision.resolvedLifeStage)
                << '\n'
                << "quality="
                << imagegen::toString(
                    resolved.quality)
                << '\n'
                << "aspect_ratio="
                << imagegen::toString(
                    resolved.aspectRatio)
                << '\n'
                << "profile="
                << resolved.profileName
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

        return GeneratedImageOutcome{
            .artifact = std::move(artifact),
            .profileName = resolved.profileName,
            .width = request.width,
            .height = request.height
        };
    }

} // namespace rose::tools
