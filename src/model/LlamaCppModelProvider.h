#pragma once

#include "model/IModelProvider.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace rose::model
{

    // LlamaCppConfig
    // -----------------------------------------------------------------------------
    // User/application-facing configuration for the llama.cpp model provider.
    //
    // This struct deliberately contains settings Rose may reasonably want to
    // configure later through:
    //
    //     - configuration files
    //     - command-line arguments
    //     - the desktop settings UI
    //     - hardware auto-detection
    //
    // It does NOT expose raw llama.cpp structures. That prevents the third-party
    // library from leaking throughout Rose's architecture.
    struct LlamaCppConfig
    {
        // Location of the GGUF model on disk.
        //
        // Model files remain outside Git because they can be several gigabytes and
        // may have their own redistribution licenses.
        std::filesystem::path modelPath;

        // Maximum amount of context available for the current inference request.
        //
        // Later this will need to account for:
        //
        //     system instructions
        //     working conversation
        //     retrieved memories
        //     documents
        //     tool results
        //
        // For our first test, 4096 is deliberately conservative.
        std::uint32_t contextSize{ 4096 };

        // Maximum number of tokens the model may generate for one response.
        std::int32_t maxGeneratedTokens{ 256 };

        // Number of transformer layers to place on a GPU backend.
        //
        // Zero means CPU-only.
        //
        // We begin at zero so that basic llama.cpp integration and GPU integration
        // are two separate debugging problems.
        std::int32_t gpuLayers{ 0 };
    };


    // LlamaCppModelProvider
    // -----------------------------------------------------------------------------
    // Concrete ModelProvider implemented using llama.cpp.
    //
    // RESPONSIBILITY:
    //
    //     Convert Rose's model request into llama.cpp inference and return text.
    //
    // DOES NOT OWN:
    //
    //     - Rose's personality
    //     - Rose's memory
    //     - conversation history
    //     - tool selection
    //     - agent planning
    //
    // Those belong to higher Rose layers.
    //
    // IMPORTANT ARCHITECTURE BOUNDARY:
    //
    // RoseCore only sees IModelProvider.
    //
    // Nothing outside this implementation needs to know that llama.cpp exists.
    class LlamaCppModelProvider final : public IModelProvider
    {
    public:
        explicit LlamaCppModelProvider(LlamaCppConfig config);

        // Declared here but defined in the .cpp because Impl is intentionally an
        // incomplete type in this header.
        ~LlamaCppModelProvider() override;

        [[nodiscard]]
        std::string generate(std::string_view input) override;

    private:
        // PImpl ("pointer to implementation").
        //
        // WHY:
        // llama.cpp types and headers remain completely inside the .cpp file.
        //
        // Benefits:
        //
        //     - reduced coupling
        //     - less recompilation when llama.cpp headers change
        //     - cleaner Rose headers
        //     - easier replacement of llama.cpp later
        struct Impl;

        std::unique_ptr<Impl> impl_;
    };

} // namespace rose::model
