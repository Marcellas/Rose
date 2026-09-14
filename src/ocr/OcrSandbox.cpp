#include "ocr/TesseractCliOcrEngine.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

int main(
    const int argc,
    char** argv)
{
    try
    {
        rose::ocr::TesseractCliOcrEngine ocr;

        std::cout
            << ocr.availabilityMessage()
            << "\n";

        if (!ocr.available())
        {
            return 2;
        }

        if (argc < 2)
        {
            std::cout
                << "Usage: RoseOcrSandbox <image-file>\n";
            return 0;
        }

        const std::filesystem::path path{
            argv[1]
        };

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

        std::vector<std::uint8_t> bytes{
            std::istreambuf_iterator<char>{ stream },
            std::istreambuf_iterator<char>{}
        };

        const rose::ocr::OcrResult result =
            ocr.recognizeEncodedImage(
                bytes,
                path.extension().string());

        std::cout
            << "\n--- OCR RESULT ---\n"
            << result.text
            << "\n--- END OCR RESULT ---\n";

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "OCR sandbox error: "
            << exception.what()
            << '\n';

        return 1;
    }
}
