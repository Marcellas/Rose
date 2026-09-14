#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rose::ocr
{

    // Uncompressed 32-bit BGRA image used at the OCR boundary.
    //
    // The buffer is top-down and tightly described by strideBytes. Keeping this
    // small value type independent of SDL, PDFium, and Tesseract means future OCR
    // providers can reuse the same input contract.
    struct OcrBitmap
    {
        int width{ 0 };
        int height{ 0 };
        std::size_t strideBytes{ 0 };
        std::vector<std::uint8_t> bgra;
    };


    struct OcrResult
    {
        std::string text;
        bool truncated{ false };
    };

} // namespace rose::ocr
