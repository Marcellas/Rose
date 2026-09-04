#pragma once

#include "model/IModelProvider.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace rose::logging
{
    class Logger;
}

namespace rose::model
{

    // LlamaCppConfig
    // -------------------------------------------------------------------------
    // Configuration describing the loaded llama.cpp runtime/model.
    //
    // Notice what is NOT here anymore:
    //
    //     maxGeneratedTokens
    //     temperature
    //     top-p
    //     top-k
    //
    // Those settings belong to individual ModelRequests.
    struct LlamaCppConfig
    {
        std::filesystem::path modelPath;

        std::uint32_t contextSize{ 4096 };

        // Number of transformer layers assigned to a GPU backend.
        //
        // Zero currently gives us the known-working CPU baseline.
        std::int32_t gpuLayers{ 0 };
    };

    class LlamaCppModelProvider final : public IModelProvider
    {
    public:
        explicit LlamaCppModelProvider(
            LlamaCppConfig config,
            rose::logging::Logger& logger);

        ~LlamaCppModelProvider() override;

        [[nodiscard]]
        ModelResponse generate(
            const ModelRequest& request) override;

    private:
        struct Impl;

        std::unique_ptr<Impl> impl_;
    };

} // namespace rose::model