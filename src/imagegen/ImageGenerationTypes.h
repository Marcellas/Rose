#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rose::imagegen
{

    // Concrete provider-facing request. Agent/tool routing should prefer
    // ImageGenerationIntent from ImageGenerationProfiles.h and resolve it
    // through the active profile set before reaching IImageGenerator.
    struct ImageGenerationRequest
    {
        std::string prompt;
        std::string negativePrompt;

        int width{ 512 };
        int height{ 512 };
        int steps{ 20 };
        float cfgScale{ 7.0f };

        // nullopt lets the backend choose/randomize its seed.
        std::optional<std::int64_t> seed;
    };


    struct ImageGenerationProvenanceEntry
    {
        // Provider-defined diagnostic metadata. Keys should be stable, ASCII,
        // single-line identifiers; values should also remain single-line so the
        // Rose sidecar stays easy to inspect and parse manually.
        std::string key;
        std::string value;
    };


    struct ImageGenerationResult
    {
        std::filesystem::path outputPath;
        std::string providerName;
        std::optional<std::int64_t> seed;

        // Optional provider process log retained beside the generated artifact.
        // This is intentionally a path rather than the entire log contents so
        // normal generation does not duplicate potentially large diagnostic text
        // in memory.
        std::filesystem::path diagnosticLogPath;

        // Provider-specific provenance that the tool layer persists into the
        // format-neutral .rosemeta.txt sidecar. This keeps IImageGenerator
        // replaceable without teaching GenerateImageTool stable-diffusion.cpp
        // concepts such as VAE/text-encoder paths.
        std::vector<ImageGenerationProvenanceEntry> provenance;
    };

} // namespace rose::imagegen
