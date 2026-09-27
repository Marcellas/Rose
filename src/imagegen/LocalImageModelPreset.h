#pragma once

#include "imagegen/ImageGenerationProfiles.h"
#include "imagegen/LocalImageModelPreference.h"
#include "imagegen/StableDiffusionCliGenerator.h"

#include <filesystem>
#include <string>

namespace rose::imagegen
{

    // Complete startup selection for one local image model.
    //
    // This is intentionally a value object. It owns paths and tuning values but no
    // model/runtime resources. StableDiffusionCliGenerator remains the process
    // adapter and owns the actual child-process execution lifetime.
    struct LocalImageModelPreset
    {
        std::string id;
        std::string displayName;

        StableDiffusionCliConfig generatorConfig;
        ImageGenerationProfileSet profiles;

        bool modernModel{ false };
    };


    // Select a configured local model without making model downloads a build-time
    // dependency.
    //
    // Auto priority intentionally preserves FLUX as Rose's first choice when its
    // component set is complete, then falls through the installed SDXL-family
    // checkpoints before retaining the legacy SD1.5 compatibility fallback.
    //
    // Expected Rose-owned filenames:
    //
    //   models/imagegen/flux2-klein-4b/diffusion.gguf
    //   models/imagegen/flux2-klein-4b/text_encoder.gguf
    //   models/imagegen/flux2-klein-4b/ae.safetensors
    //   models/imagegen/realvisxl-v5/model.safetensors
    //   models/imagegen/juggernaut-xl/model.safetensors
    //   models/imagegen/pony-v6/model.safetensors
    //   models/imagegen/v1-5-pruned-emaonly.safetensors
    [[nodiscard]]
    LocalImageModelPreset selectLocalImageModelPreset(
        const std::filesystem::path& imageModelRoot,
        LocalImageModelPreference preference =
            LocalImageModelPreference::Auto);

} // namespace rose::imagegen
