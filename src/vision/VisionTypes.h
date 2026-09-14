#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rose::vision
{

    struct VisionRequest
    {
        std::span<const std::uint8_t> encodedImage;
        std::string_view sourceExtension;
        std::string_view userPrompt;
    };


    struct VisionResult
    {
        // Provider-generated visual observations in UTF-8.
        std::string text;

        // True when Rose deliberately clipped provider output before returning it
        // to higher layers.
        bool truncated{ false };
    };

} // namespace rose::vision
