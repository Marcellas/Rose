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
        // Empty means auto-discover sd-cli.exe in Rose's normal development/runtime
        // locations and then PATH.
        std::filesystem::path executablePath;

        // MVP: one normal checkpoint/model file accepted by stable-diffusion.cpp's
        // --model argument. Newer multi-component models can be added later without
        // changing IImageGenerator.
        std::filesystem::path modelPath;

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
