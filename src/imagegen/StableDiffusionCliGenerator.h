#pragma once

#include "imagegen/IImageGenerator.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rose::imagegen
{

    struct StableDiffusionCliConfig
    {
        // Rose-owned logical model identity. These fields do not affect the
        // backend command line; they travel with provider diagnostics so every
        // generated artifact can explain which configured preset produced it.
        std::string modelId;
        std::string modelDisplayName;

        // Empty means auto-discover sd-cli.exe in Rose's normal development/runtime
        // locations and then PATH.
        std::filesystem::path executablePath;

        // Legacy/single-file pipeline loaded through stable-diffusion.cpp's
        // --model argument (for example SD1.5/SDXL checkpoints).
        std::filesystem::path modelPath;

        // Modern componentized pipeline loaded through:
        //
        //     --diffusion-model
        //     --vae
        //     --llm
        //
        // When diffusionModelPath is non-empty, modelPath must be empty and all
        // three component paths below are required.
        std::filesystem::path diffusionModelPath;
        std::filesystem::path vaePath;
        std::filesystem::path llmPath;

        // Provider flags used by current transformer-based image models.
        bool diffusionFlashAttention{ false };
        bool offloadToCpu{ false };

        // Optional stable-diffusion.cpp backend assignments, for example:
        //     cuda0
        //     diffusion=cuda0,te=cpu,vae=cpu
        std::string backendAssignment;
        std::string paramsBackendAssignment;
        std::string maxVramAssignment;

        bool autoFit{ true };

        // Developer-controlled arguments only. User prompt text never enters here.
        std::vector<std::string> extraArguments;

        std::chrono::milliseconds timeout{
            std::chrono::minutes{ 10 }
        };
    };


    // Local/offline image generator implemented as a strict process adapter around
    // stable-diffusion.cpp's sd-cli executable.
    //
    // Keeping diffusion in a child process has two useful MVP properties:
    //     1. Rose does not link a second ggml copy into its own process.
    //     2. Diffusion VRAM/RAM is deterministically released when sd-cli exits.
    class StableDiffusionCliGenerator final : public IImageGenerator
    {
    public:
        explicit StableDiffusionCliGenerator(
            StableDiffusionCliConfig config);

        ~StableDiffusionCliGenerator() override;

        StableDiffusionCliGenerator(
            const StableDiffusionCliGenerator&) = delete;

        StableDiffusionCliGenerator& operator=(
            const StableDiffusionCliGenerator&) = delete;

        [[nodiscard]]
        bool available() const noexcept override;

        [[nodiscard]]
        std::string availabilityMessage() const override;

        [[nodiscard]]
        ImageGenerationResult generate(
            const ImageGenerationRequest& request,
            const std::filesystem::path& destinationPath) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace rose::imagegen
