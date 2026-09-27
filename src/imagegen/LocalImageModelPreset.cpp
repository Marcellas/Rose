#include "imagegen/LocalImageModelPreset.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace rose::imagegen
{
    namespace
    {
        using ProfileFactory =
            ImageGenerationProfileSet (*)();


        struct SingleCheckpointDefinition
        {
            LocalImageModelPreference preference;
            std::string_view id;
            std::string_view displayName;
            std::filesystem::path relativePath;
            ProfileFactory makeProfiles;
            bool modernModel;

            // 0 means let stable-diffusion.cpp/model defaults decide. Positive
            // values are passed through as --clip-skip N.
            int clipSkip{ 0 };
        };


        // Keep the single-checkpoint models data-driven. Adding another normal
        // SD/SDXL checkpoint should usually require one table entry, one preference
        // enum/parser value, and (when tuning differs) one profile factory.
        [[nodiscard]]
        const std::array<SingleCheckpointDefinition, 4>&
            singleCheckpointDefinitions()
        {
            static const std::array<SingleCheckpointDefinition, 4> definitions{
                SingleCheckpointDefinition{
                    .preference =
                        LocalImageModelPreference::RealVisXLV5,
                    .id =
                        "realvisxl-v5",
                    .displayName =
                        "RealVisXL V5",
                    .relativePath =
                        std::filesystem::path{ "realvisxl-v5" }
                        / "model.safetensors",
                    .makeProfiles =
                        &makeRealVisXLV5Profiles,
                    .modernModel =
                        true,
                    .clipSkip =
                        0
                },
                SingleCheckpointDefinition{
                    .preference =
                        LocalImageModelPreference::JuggernautXL,
                    .id =
                        "juggernaut-xl",
                    .displayName =
                        "Juggernaut XL",
                    .relativePath =
                        std::filesystem::path{ "juggernaut-xl" }
                        / "model.safetensors",
                    .makeProfiles =
                        &makeJuggernautXLProfiles,
                    .modernModel =
                        true,
                    .clipSkip =
                        0
                },
                SingleCheckpointDefinition{
                    .preference =
                        LocalImageModelPreference::PonyV6,
                    .id =
                        "pony-v6",
                    .displayName =
                        "Pony Diffusion V6 XL",
                    .relativePath =
                        std::filesystem::path{ "pony-v6" }
                        / "model.safetensors",
                    .makeProfiles =
                        &makePonyV6Profiles,
                    .modernModel =
                        true,
                    .clipSkip =
                        2
                },
                SingleCheckpointDefinition{
                    .preference =
                        LocalImageModelPreference::
                            StableDiffusion15Fallback,
                    .id =
                        "sd15-fallback",
                    .displayName =
                        "Stable Diffusion 1.5 (fallback)",
                    .relativePath =
                        "v1-5-pruned-emaonly.safetensors",
                    .makeProfiles =
                        &makeStableDiffusion15Profiles,
                    .modernModel =
                        false,
                    .clipSkip =
                        0
                }
            };

            return definitions;
        }


        struct FluxPaths
        {
            std::filesystem::path diffusion;
            std::filesystem::path textEncoder;
            std::filesystem::path vae;
        };


        [[nodiscard]]
        FluxPaths makeFluxPaths(
            const std::filesystem::path& imageModelRoot)
        {
            const std::filesystem::path root =
                imageModelRoot
                / "flux2-klein-4b";

            return FluxPaths{
                .diffusion =
                    root / "diffusion.gguf",
                .textEncoder =
                    root / "text_encoder.gguf",
                .vae =
                    root / "ae.safetensors"
            };
        }


        [[nodiscard]]
        bool fluxComplete(
            const FluxPaths& paths)
        {
            return
                std::filesystem::exists(
                    paths.diffusion)
                && std::filesystem::exists(
                    paths.textEncoder)
                && std::filesystem::exists(
                    paths.vae);
        }


        [[nodiscard]]
        const SingleCheckpointDefinition* findSingleCheckpointDefinition(
            const LocalImageModelPreference preference)
        {
            for (const auto& definition :
                 singleCheckpointDefinitions())
            {
                if (definition.preference == preference)
                {
                    return &definition;
                }
            }

            return nullptr;
        }


        [[nodiscard]]
        bool checkpointAvailable(
            const std::filesystem::path& imageModelRoot,
            const SingleCheckpointDefinition& definition)
        {
            return std::filesystem::exists(
                imageModelRoot
                / definition.relativePath);
        }


        [[nodiscard]]
        LocalImageModelPreset makeFluxPreset(
            const FluxPaths& paths)
        {
            StableDiffusionCliConfig generatorConfig;
            generatorConfig.modelId =
                "flux2-klein-4b";
            generatorConfig.modelDisplayName =
                "FLUX.2 Klein 4B";
            generatorConfig.diffusionModelPath =
                paths.diffusion;
            generatorConfig.vaePath =
                paths.vae;
            generatorConfig.llmPath =
                paths.textEncoder;
            generatorConfig.diffusionFlashAttention =
                true;

            // Upstream FLUX.2 Klein examples recommend CPU offload. Rose also
            // keeps her conversational Qwen model resident on this same GPU, so
            // the image child process should not compete for the full 16 GiB VRAM
            // pool.
            generatorConfig.offloadToCpu =
                true;

            // Limit the child process to an 8 GiB managed budget and let
            // stable-diffusion.cpp auto-fit/offload within Rose's 64 GiB system-RAM
            // envelope.
            generatorConfig.maxVramAssignment =
                "cuda0=8";
            generatorConfig.autoFit =
                true;

            return LocalImageModelPreset{
                .id =
                    "flux2-klein-4b",
                .displayName =
                    "FLUX.2 Klein 4B",
                .generatorConfig =
                    std::move(generatorConfig),
                .profiles =
                    makeFlux2Klein4BProfiles(),
                .modernModel =
                    true
            };
        }


        [[nodiscard]]
        LocalImageModelPreset makeSingleCheckpointPreset(
            const std::filesystem::path& imageModelRoot,
            const SingleCheckpointDefinition& definition)
        {
            StableDiffusionCliConfig generatorConfig;
            generatorConfig.modelId =
                definition.id;
            generatorConfig.modelDisplayName =
                definition.displayName;
            generatorConfig.modelPath =
                imageModelRoot
                / definition.relativePath;

            // All current local image presets share Rose's conservative child
            // process VRAM ceiling because the conversational model remains loaded.
            generatorConfig.maxVramAssignment =
                "cuda0=8";
            generatorConfig.autoFit =
                true;

            if (definition.clipSkip > 0)
            {
                generatorConfig.extraArguments.push_back(
                    "--clip-skip");
                generatorConfig.extraArguments.push_back(
                    std::to_string(
                        definition.clipSkip));
            }

            return LocalImageModelPreset{
                .id =
                    std::string{ definition.id },
                .displayName =
                    std::string{ definition.displayName },
                .generatorConfig =
                    std::move(generatorConfig),
                .profiles =
                    definition.makeProfiles(),
                .modernModel =
                    definition.modernModel
            };
        }


        [[nodiscard]]
        std::runtime_error missingCheckpointError(
            const std::filesystem::path& imageModelRoot,
            const SingleCheckpointDefinition& definition)
        {
            const std::filesystem::path expectedPath =
                imageModelRoot
                / definition.relativePath;

            return std::runtime_error{
                "Image model preference '"
                + std::string{ definition.id }
                + "' is selected, but its checkpoint was not found at "
                + expectedPath.string()
                + "."
            };
        }
    }


    LocalImageModelPreset selectLocalImageModelPreset(
        const std::filesystem::path& imageModelRoot,
        const LocalImageModelPreference preference)
    {
        const FluxPaths fluxPaths =
            makeFluxPaths(
                imageModelRoot);

        const bool fluxAvailable =
            fluxComplete(
                fluxPaths);

        if (preference == LocalImageModelPreference::Auto)
        {
            if (fluxAvailable)
            {
                return makeFluxPreset(
                    fluxPaths);
            }

            // Auto order after FLUX is intentionally explicit and stable. This
            // prevents adding a future checkpoint from silently changing which
            // existing installed model a user's workstation selects.
            constexpr std::array<LocalImageModelPreference, 4> autoPriority{
                LocalImageModelPreference::RealVisXLV5,
                LocalImageModelPreference::JuggernautXL,
                LocalImageModelPreference::PonyV6,
                LocalImageModelPreference::StableDiffusion15Fallback
            };

            for (const LocalImageModelPreference candidate : autoPriority)
            {
                const SingleCheckpointDefinition* definition =
                    findSingleCheckpointDefinition(
                        candidate);

                if (
                    definition != nullptr
                    && checkpointAvailable(
                        imageModelRoot,
                        *definition))
                {
                    return makeSingleCheckpointPreset(
                        imageModelRoot,
                        *definition);
                }
            }

            // Preserve Rose's historical graceful startup behavior when no image
            // checkpoint is present. The generator will later report the missing
            // SD1.5 file if image generation is actually requested.
            const SingleCheckpointDefinition* fallback =
                findSingleCheckpointDefinition(
                    LocalImageModelPreference::
                        StableDiffusion15Fallback);

            if (fallback == nullptr)
            {
                throw std::logic_error{
                    "Rose's SD1.5 fallback model definition is missing."
                };
            }

            return makeSingleCheckpointPreset(
                imageModelRoot,
                *fallback);
        }


        if (preference == LocalImageModelPreference::Flux2Klein4B)
        {
            if (!fluxAvailable)
            {
                throw std::runtime_error{
                    "Image model preference 'flux2-klein-4b' is selected, but its "
                    "required files are incomplete under models/imagegen/"
                    "flux2-klein-4b. Expected diffusion.gguf, text_encoder.gguf, "
                    "and ae.safetensors."
                };
            }

            return makeFluxPreset(
                fluxPaths);
        }


        const SingleCheckpointDefinition* definition =
            findSingleCheckpointDefinition(
                preference);

        if (definition == nullptr)
        {
            throw std::runtime_error{
                "Unknown local image-model preference."
            };
        }

        if (!checkpointAvailable(
                imageModelRoot,
                *definition))
        {
            throw missingCheckpointError(
                imageModelRoot,
                *definition);
        }

        return makeSingleCheckpointPreset(
            imageModelRoot,
            *definition);
    }

} // namespace rose::imagegen
