#pragma once

#include "ocr/OcrTypes.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rose::ocr
{

    // Provider-neutral OCR boundary.
    //
    // Rose's document tools depend on this interface rather than Tesseract itself.
    // That lets us replace the implementation later with a linked Tesseract API,
    // ONNX OCR model, cloud OCR provider, or another local engine without changing
    // PDF/image ingestion.
    class IOcrEngine
    {
    public:
        virtual ~IOcrEngine() = default;

        [[nodiscard]]
        virtual bool available() const noexcept = 0;

        [[nodiscard]]
        virtual std::string availabilityMessage() const = 0;

        // OCR an encoded image container such as PNG/JPEG/BMP/TIFF.
        [[nodiscard]]
        virtual OcrResult recognizeEncodedImage(
            std::span<const std::uint8_t> bytes,
            std::string_view sourceExtension) = 0;

        // OCR an already-rasterized BGRA image, used by scanned PDF pages.
        [[nodiscard]]
        virtual OcrResult recognizeBitmap(
            const OcrBitmap& bitmap) = 0;
    };

} // namespace rose::ocr
