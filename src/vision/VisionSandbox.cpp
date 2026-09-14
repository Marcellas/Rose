#include "vision/LlamaMtmdVisionProvider.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]]
    std::vector<std::uint8_t> readAllBytes(
        const std::filesystem::path& path)
    {
        std::ifstream stream{
            path,
            std::ios::binary
        };

        if (!stream)
        {
            throw std::runtime_error{
                "Could not open image: "
                + path.string()
            };
        }

        return std::vector<std::uint8_t>{
            std::istreambuf_iterator<char>{ stream },
            std::istreambuf_iterator<char>{}
        };
    }
}


int main(
    const int argc,
    char** argv)
{
    try
    {
        rose::vision::LlamaMtmdVisionProvider provider;

        std::cout
            << provider.availabilityMessage()
            << '\n';

        if (!provider.available())
        {
            return 1;
        }

        if (argc < 2)
        {
            std::cout
                << "Usage: RoseVisionSandbox <image-file> [question]\n";

            return 0;
        }

        const std::filesystem::path imagePath{
            argv[1]
        };

        const std::vector<std::uint8_t> bytes =
            readAllBytes(
                imagePath);

        std::string prompt =
            argc >= 3
                ? std::string{
                    argv[2]
                }
                : std::string{
                    "Describe this image and identify the most important visible details."
                };

        const rose::vision::VisionResult result =
            provider.analyze(
                rose::vision::VisionRequest{
                    .encodedImage = bytes,
                    .sourceExtension =
                        imagePath.extension().string(),
                    .userPrompt = prompt
                });

        std::cout
            << "\n--- VISION RESULT ---\n"
            << result.text
            << "\n--- END VISION RESULT ---\n";

        if (result.truncated)
        {
            std::cout
                << "[Rose clipped the vision result at its configured output limit.]\n";
        }

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Vision sandbox error: "
            << exception.what()
            << '\n';

        return 1;
    }
}
