#pragma once

#include "ocr/IOcrEngine.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace rose::ocr
{

    struct TesseractCliOcrConfig
    {
        // Optional explicit executable. When empty Rose searches a few sensible
        // development/install locations and finally PATH.
        std::filesystem::path executablePath;

        // Optional tessdata directory. When empty Tesseract's own default is used.
        std::filesystem::path tessdataDirectory;

        std::string language{ "eng" };

        // Automatic page segmentation works well for ordinary documents and
        // screenshots while remaining conservative for the first OCR checkpoint.
        int pageSegmentationMode{ 3 };

        // Prevent a damaged OCR process from blocking Rose's worker indefinitely.
        std::chrono::milliseconds timeout{
            std::chrono::seconds{ 90 }
        };

        // OCR output is transient model context. Keep one recognition bounded even
        // before the higher-level PDF/document budget is applied.
        std::size_t maximumOutputUtf8Bytes{ 64u * 1024u };
    };


    // Windows adapter around a local Tesseract executable.
    //
    // WHY CLI FIRST:
    // The current Rose build remains ordinary MSVC/CMake and does not need to link
    // Tesseract + Leptonica + codec libraries. This adapter proves the OCR vertical
    // slice while preserving IOcrEngine, so we can later replace it with an in-
    // process implementation without touching document ingestion.
    //
    // SECURITY:
    // Tesseract never receives the user's original file path. Rose first copies
    // exact authorized bytes into a private temporary file. Scanned PDF pages are
    // rasterized by PDFium and written to a temporary BMP. The child process only
    // sees those Rose-owned temporary files.
    class TesseractCliOcrEngine final : public IOcrEngine
    {
    public:
        explicit TesseractCliOcrEngine(
            TesseractCliOcrConfig config = {});

        [[nodiscard]]
        bool available() const noexcept override;

        [[nodiscard]]
        std::string availabilityMessage() const override;

        [[nodiscard]]
        OcrResult recognizeEncodedImage(
            std::span<const std::uint8_t> bytes,
            std::string_view sourceExtension) override;

        [[nodiscard]]
        OcrResult recognizeBitmap(
            const OcrBitmap& bitmap) override;

    private:
        [[nodiscard]]
        OcrResult recognizeTemporaryImage(
            const std::filesystem::path& imagePath);

        [[nodiscard]]
        std::filesystem::path makeTemporaryStem() const;

        TesseractCliOcrConfig config_;
        std::filesystem::path executablePath_;
        std::filesystem::path tessdataDirectory_;
        std::string unavailableReason_;
    };

} // namespace rose::ocr
