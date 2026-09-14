#pragma once

#include "tools/ReadFileTool.h"

#include <cstddef>
#include <string>

namespace rose::ocr
{
    class IOcrEngine;
}

namespace rose::tools
{

    struct PdfTextExtractorConfig
    {
        // Extracted document text is transient model context. Keep it bounded.
        std::size_t maximumExtractedUtf8Bytes{ 24u * 1024u };

        // Embedded text shorter than this is treated as suspicious (often only a
        // page number/watermark on an otherwise scanned page) and OCR is attempted.
        std::size_t minimumEmbeddedNonWhitespaceCharacters{ 24u };

        // Scanned documents can be enormous. OCR is deliberately bounded for one
        // interactive request; long-document retrieval comes later.
        std::size_t maximumOcrPages{ 12u };

        // Roughly 200 DPI is a good first-pass balance for ordinary office scans.
        float ocrRenderDpi{ 200.0f };

        // Bounds raster memory and OCR cost even for unusually large PDF pages.
        int maximumOcrImageDimension{ 3000 };
    };


    struct ExtractedPdfDocument
    {
        std::string text;
        int pageCount{ 0 };
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
            ocr::IOcrEngine& ocrEngine) const;

    private:
        PdfTextExtractorConfig config_;
    };

} // namespace rose::tools
