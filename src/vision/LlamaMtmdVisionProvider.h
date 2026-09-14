#pragma once

#include "vision/IVisionProvider.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace rose::vision
{

    // Configuration for Rose's first local semantic-vision provider.
    //
    // The provider intentionally runs multimodal inference in a short-lived helper
    // process rather than loading a second llama.cpp runtime inside Rose.exe.
    //
    // WHY:
    //     Rose's current text provider owns llama_backend_init()/free(). Keeping the
    //     vision model in a process isolates that lifetime, releases its VRAM after
    //     each visual request, and avoids coupling Rose to libmtmd's experimental API.
    //
    // LATER:
    //     Once Rose has a shared process-level llama runtime and model residency
    //     coordinator, IVisionProvider can gain an in-process implementation without
    //     changing AttachmentIngestion.
    struct LlamaMtmdVisionConfig
    {
        // Empty means auto-discover RoseVisionWorker beside Rose.exe.
        std::filesystem::path workerExecutable;

        std::filesystem::path modelPath{
            "models/vision/Qwen3VL-8B-Instruct-Q4_K_M.gguf"
        };

        std::filesystem::path mmprojPath{
            "models/vision/mmproj-Qwen3VL-8B-Instruct-Q8_0.gguf"
        };

        std::uint32_t contextSize{ 4096 };
        std::int32_t maxGeneratedTokens{ 512 };

        // Same convention as Rose's existing llama.cpp provider. 999 effectively
        // means "offload every model layer that exists".
        std::int32_t gpuLayers{ 999 };

        bool mmprojUseGpu{ true };

        // Dynamic-resolution models can otherwise consume most of the context with
        // image tokens. Keep the first vision slice bounded and responsive.
        std::int32_t imageMaxTokens{ 1536 };

        std::chrono::milliseconds timeout{
            std::chrono::minutes{ 4 }
        };

        std::size_t maximumOutputUtf8Bytes{
            96u * 1024u
        };
    };


    // Process-isolated llama.cpp/libmtmd vision provider.
    //
    // OWNERSHIP:
    //     - owns only configuration
    //     - each analyze() call owns its temporary image/prompt/result files
    //     - RoseVisionWorker owns the vision model while that subprocess is alive
    //
    // DATA FLOW:
    //     authorized image bytes
    //          -> private temp image
    //          -> RoseVisionWorker + local GGUF/mmproj
    //          -> UTF-8 visual observations
    //          -> temp files deleted
    class LlamaMtmdVisionProvider final : public IVisionProvider
    {
    public:
        explicit LlamaMtmdVisionProvider(
            LlamaMtmdVisionConfig config = {});

        ~LlamaMtmdVisionProvider() override;

        LlamaMtmdVisionProvider(
            const LlamaMtmdVisionProvider&) = delete;

        LlamaMtmdVisionProvider& operator=(
            const LlamaMtmdVisionProvider&) = delete;

        [[nodiscard]]
        bool available() const noexcept override;

        [[nodiscard]]
        std::string availabilityMessage() const override;

        [[nodiscard]]
        VisionResult analyze(
            const VisionRequest& request) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace rose::vision
