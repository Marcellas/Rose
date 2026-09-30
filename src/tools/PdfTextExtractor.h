#pragma once

#include "tools/ReadFileTool.h"

#include <cstddef>
#include <optional>
#include <string>

namespace rose::ocr
{
    class IOcrEngine;
}

namespace rose::tools
{

    struct PdfTextExtractorConfig
    {
        // Limit one extraction window; ReadPdfRegisteredTool advances through
        // the requested pages in repeated windows rather than stopping here.
        std::size_t maximumExtractedUtf8Bytes{ 24u * 1024u };

        // Embedded text shorter than this is treated as suspicious (often only a
        // page number/watermark on an otherwise scanned page) and OCR is attempted.
        std::size_t minimumEmbeddedNonWhitespaceCharacters{ 24u };

        // Bound OCR work per extraction window. The registered reader continues
        // with the next window to cover a larger requested page range.
        std::size_t maximumOcrPages{ 12u };

        // Roughly 200 DPI is a good first-pass balance for ordinary office scans.
        float ocrRenderDpi{ 200.0f };

        // Bounds raster memory and OCR cost even for unusually large PDF pages.
        int maximumOcrImageDimension{ 3000 };
    };



    struct PdfPageSelection
    {
        // One-based page index. Defaults to the beginning of the document.
        std::size_t startPage{ 1u };

        // When absent, extraction continues through the end of the document.
        // When present, only this many pages are selected (or the remaining
        // document pages when the request extends beyond the end).
        std::optional<std::size_t> pageCount{};
    };


    struct ExtractedPdfDocument
    {
        std::string text;
        // Total pages in the source PDF, independent of a requested window.
        int pageCount{ 0 };

        // One-based page window actually selected from the source document.
        int selectedPageStart{ 0 };
        int selectedPageEnd{ 0 };
        int selectedPageCount{ 0 };
        int pagesExamined{ 0 };

        int pagesWithText{ 0 };
        int pagesWithEmbeddedText{ 0 };
        int pagesOcred{ 0 };
        int pagesWithoutText{ 0 };

        bool truncated{ false };

        // True when one or more pages needed OCR but no OCR provider was available.
        bool requiresOcr{ false };
    };


    // PDFium-backed PDF extractor with OCR fallback.
    //
    // Searchable pages stay cheap: PDFium's text layer is used directly.
    // Image-only/suspicious pages are rasterized by PDFium and passed through the
    // provider-neutral IOcrEngine. This lets mixed PDFs work without OCRing every
    // page unnecessarily.
    class PdfTextExtractor final
    {
    public:
        explicit PdfTextExtractor(
            PdfTextExtractorConfig config = {});

        ~PdfTextExtractor();

        PdfTextExtractor(const PdfTextExtractor&) = delete;
        PdfTextExtractor& operator=(const PdfTextExtractor&) = delete;

        PdfTextExtractor(PdfTextExtractor&&) = delete;
        PdfTextExtractor& operator=(PdfTextExtractor&&) = delete;

        [[nodiscard]]
        ExtractedPdfDocument extract(
            const ReadBinaryFileResult& file,
            ocr::IOcrEngine& ocrEngine,
            PdfPageSelection selection = {}) const;

    private:
        PdfTextExtractorConfig config_;
    };

} // namespace rose::tools
