#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace rose::imagegen
{

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


    struct ImageGenerationResult
    {
        std::filesystem::path outputPath;
        std::string providerName;
        std::optional<std::int64_t> seed;
    };

} // namespace rose::imagegen
