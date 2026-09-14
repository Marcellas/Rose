#pragma once

#include "imagegen/ImageGenerationTypes.h"

#include <filesystem>
#include <string>

namespace rose::imagegen
{

    // Replaceable image-generation capability.
    //
    // RoseCore does not know which image backend exists. The tool layer chooses an
    // output destination inside Rose's artifact store, then asks the provider to
    // create the image there.
    class IImageGenerator
    {
    public:
        virtual ~IImageGenerator() = default;

        [[nodiscard]]
        virtual bool available() const noexcept = 0;

        [[nodiscard]]
        virtual std::string availabilityMessage() const = 0;

        [[nodiscard]]
        virtual ImageGenerationResult generate(
            const ImageGenerationRequest& request,
            const std::filesystem::path& destinationPath) = 0;
    };

} // namespace rose::imagegen
