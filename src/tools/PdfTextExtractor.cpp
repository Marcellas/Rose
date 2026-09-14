#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tools/PdfTextExtractor.h"

#include "ocr/IOcrEngine.h"

#include <fpdf_text.h>
#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace rose::tools
{

    namespace
    {
        struct DocumentCloser
        {
            void operator()(FPDF_DOCUMENT document) const noexcept
            {
                if (document != nullptr)
                {
                    FPDF_CloseDocument(document);
                }
            }
        };

        struct PageCloser
        {
            void operator()(FPDF_PAGE page) const noexcept
            {
                if (page != nullptr)
                {
                    FPDF_ClosePage(page);
                }
            }
        };

        struct TextPageCloser
        {
            void operator()(FPDF_TEXTPAGE textPage) const noexcept
            {
                if (textPage != nullptr)
                {
                    FPDFText_ClosePage(textPage);
                }
            }
        };

        struct BitmapCloser
        {
            void operator()(FPDF_BITMAP bitmap) const noexcept
            {
                if (bitmap != nullptr)
                {
                    FPDFBitmap_Destroy(bitmap);
                }
            }
        };

        using DocumentPtr =
            std::unique_ptr<
                std::remove_pointer_t<FPDF_DOCUMENT>,
                DocumentCloser>;

        using PagePtr =
            std::unique_ptr<
                std::remove_pointer_t<FPDF_PAGE>,
                PageCloser>;

        using TextPagePtr =
            std::unique_ptr<
                std::remove_pointer_t<FPDF_TEXTPAGE>,
                TextPageCloser>;

        using BitmapPtr =
            std::unique_ptr<
                std::remove_pointer_t<FPDF_BITMAP>,
                BitmapCloser>;


        [[nodiscard]]
        std::string pdfiumErrorDescription(
            const unsigned long error)
        {
            switch (error)
            {
            case FPDF_ERR_SUCCESS:
                return "success";
            case FPDF_ERR_UNKNOWN:
                return "unknown PDFium error";
            case FPDF_ERR_FILE:
                return "file access error";
            case FPDF_ERR_FORMAT:
                return "invalid or unsupported PDF format";
            case FPDF_ERR_PASSWORD:
                return "password-protected PDF";
            case FPDF_ERR_SECURITY:
                return "unsupported PDF security scheme";
            case FPDF_ERR_PAGE:
                return "page access error";
            default:
                return "PDFium error code "
                    + std::to_string(error);
            }
        }


        void appendUtf8CodePoint(
            std::string& output,
            const std::uint32_t codePoint)
        {
            if (codePoint <= 0x7Fu)
            {
                output.push_back(
                    static_cast<char>(codePoint));
            }
            else if (codePoint <= 0x7FFu)
            {
                output.push_back(
                    static_cast<char>(
                        0xC0u | (codePoint >> 6u)));
                output.push_back(
                    static_cast<char>(
                        0x80u | (codePoint & 0x3Fu)));
            }
            else if (codePoint <= 0xFFFFu)
            {
                output.push_back(
                    static_cast<char>(
                        0xE0u | (codePoint >> 12u)));
                output.push_back(
                    static_cast<char>(
                        0x80u | ((codePoint >> 6u) & 0x3Fu)));
                output.push_back(
                    static_cast<char>(
                        0x80u | (codePoint & 0x3Fu)));
            }
            else
            {
                output.push_back(
                    static_cast<char>(
                        0xF0u | (codePoint >> 18u)));
                output.push_back(
                    static_cast<char>(
                        0x80u | ((codePoint >> 12u) & 0x3Fu)));
                output.push_back(
                    static_cast<char>(
                        0x80u | ((codePoint >> 6u) & 0x3Fu)));
                output.push_back(
                    static_cast<char>(
                        0x80u | (codePoint & 0x3Fu)));
            }
        }


        [[nodiscard]]
        std::string utf16ToUtf8(
            const unsigned short* units,
            const std::size_t unitCount)
        {
            std::string output;
            output.reserve(unitCount);

            std::size_t index{ 0 };

            while (index < unitCount)
            {
                const std::uint32_t first =
                    units[index++];

                if (first == 0)
                {
                    break;
                }

                if (first >= 0xD800u && first <= 0xDBFFu)
                {
                    if (index >= unitCount)
                    {
                        appendUtf8CodePoint(
                            output,
                            0xFFFDu);
                        break;
                    }

                    const std::uint32_t second =
                        units[index];

                    if (second < 0xDC00u || second > 0xDFFFu)
                    {
                        appendUtf8CodePoint(
                            output,
                            0xFFFDu);
                        continue;
                    }

                    ++index;

                    const std::uint32_t codePoint =
                        0x10000u
                        + ((first - 0xD800u) << 10u)
                        + (second - 0xDC00u);

                    appendUtf8CodePoint(
                        output,
                        codePoint);
                    continue;
                }

                if (first >= 0xDC00u && first <= 0xDFFFu)
                {
                    appendUtf8CodePoint(
                        output,
                        0xFFFDu);
                    continue;
                }

                appendUtf8CodePoint(
                    output,
                    first);
            }

            return output;
        }


        [[nodiscard]]
        std::size_t countNonWhitespace(
            const std::string_view text) noexcept
        {
            std::size_t count{ 0 };

            for (const char value : text)
            {
                switch (value)
                {
                case ' ':
                case '\t':
                case '\r':
                case '\n':
                case '\f':
                case '\v':
                    break;
                default:
                    ++count;
                    break;
                }
            }

            return count;
        }


        [[nodiscard]]
        bool containsNonWhitespace(
            const std::string_view text) noexcept
        {
            return countNonWhitespace(text) > 0;
        }


        void appendWithLimit(
            std::string& destination,
            const std::string_view source,
            const std::size_t maximumBytes,
            bool& truncated)
        {
            if (destination.size() >= maximumBytes)
            {
                truncated = true;
                return;
            }

            const std::size_t available =
                maximumBytes - destination.size();

            std::size_t amount =
                (std::min)(
                    available,
                    source.size());

            if (amount < source.size())
            {
                while (
                    amount > 0
                    && (
                        static_cast<unsigned char>(
                            source[amount])
                        & 0xC0u)
                    == 0x80u)
                {
                    --amount;
                }
            }

            destination.append(
                source.data(),
                amount);

            if (amount < source.size())
            {
                truncated = true;
            }
        }


        [[nodiscard]]
        std::string extractEmbeddedPageText(
            FPDF_PAGE page)
        {
            FPDF_TEXTPAGE rawTextPage =
                FPDFText_LoadPage(page);

            if (rawTextPage == nullptr)
            {
                return {};
            }

            TextPagePtr textPage{
                rawTextPage
            };

            const int characterCount =
                FPDFText_CountChars(
                    textPage.get());

            if (characterCount <= 0)
            {
                return {};
            }

            std::vector<unsigned short> utf16(
                static_cast<std::size_t>(characterCount)
                    + 1u,
                0u);

            const int written =
                FPDFText_GetText(
                    textPage.get(),
                    0,
                    characterCount,
                    utf16.data());

            if (written <= 1)
            {
                return {};
            }

            return utf16ToUtf8(
                utf16.data(),
                static_cast<std::size_t>(written - 1));
        }


        [[nodiscard]]
        std::pair<int, int> calculateOcrDimensions(
            FPDF_PAGE page,
            const PdfTextExtractorConfig& config)
        {
            const double widthPoints =
                FPDF_GetPageWidth(page);

            const double heightPoints =
                FPDF_GetPageHeight(page);

            if (
                widthPoints <= 0.0
                || heightPoints <= 0.0)
            {
                throw std::runtime_error{
                    "PDFium reported invalid page dimensions for OCR."
                };
            }

            constexpr double pointsPerInch{ 72.0 };

            double widthPixels =
                widthPoints
                * static_cast<double>(config.ocrRenderDpi)
                / pointsPerInch;

            double heightPixels =
                heightPoints
                * static_cast<double>(config.ocrRenderDpi)
                / pointsPerInch;

            const double largest =
                (std::max)(
                    widthPixels,
                    heightPixels);

            if (
                largest
                > static_cast<double>(
                    config.maximumOcrImageDimension))
            {
                const double scale =
                    static_cast<double>(
                        config.maximumOcrImageDimension)
                    / largest;

                widthPixels *= scale;
                heightPixels *= scale;
            }

            const int width =
                (std::max)(
                    1,
                    static_cast<int>(
                        std::lround(widthPixels)));

            const int height =
                (std::max)(
                    1,
                    static_cast<int>(
                        std::lround(heightPixels)));

            return {
                width,
                height
            };
        }


        [[nodiscard]]
        ocr::OcrBitmap renderPageForOcr(
            FPDF_PAGE page,
            const PdfTextExtractorConfig& config)
        {
            const auto [width, height] =
                calculateOcrDimensions(
                    page,
                    config);

            FPDF_BITMAP rawBitmap =
                FPDFBitmap_Create(
                    width,
                    height,
                    1);

            if (rawBitmap == nullptr)
            {
                throw std::runtime_error{
                    "PDFium could not allocate the scanned-page OCR bitmap."
                };
            }

            BitmapPtr bitmap{
                rawBitmap
            };

            // White page background. PDFium's color is ARGB.
            FPDFBitmap_FillRect(
                bitmap.get(),
                0,
                0,
                width,
                height,
                0xFFFFFFFFu);

            FPDF_RenderPageBitmap(
                bitmap.get(),
                page,
                0,
                0,
                width,
                height,
                0,
                FPDF_ANNOT);

            const int sourceStride =
                FPDFBitmap_GetStride(
                    bitmap.get());

            void* rawPixels =
                FPDFBitmap_GetBuffer(
                    bitmap.get());

            if (
                sourceStride <= 0
                || rawPixels == nullptr)
            {
                throw std::runtime_error{
                    "PDFium returned an invalid OCR bitmap buffer."
                };
            }

            const std::size_t destinationStride =
                static_cast<std::size_t>(width)
                * 4u;

            ocr::OcrBitmap result;
            result.width = width;
            result.height = height;
            result.strideBytes = destinationStride;
            result.bgra.resize(
                destinationStride
                * static_cast<std::size_t>(height));

            const auto* source =
                static_cast<const std::uint8_t*>(
                    rawPixels);

            for (int y = 0;
                 y < height;
                 ++y)
            {
                const std::uint8_t* sourceRow =
                    source
                    + static_cast<std::size_t>(y)
                        * static_cast<std::size_t>(sourceStride);

                std::uint8_t* destinationRow =
                    result.bgra.data()
                    + static_cast<std::size_t>(y)
                        * destinationStride;

                std::memcpy(
                    destinationRow,
                    sourceRow,
                    destinationStride);

                // Normalize alpha. The OCR engine does not care about transparency,
                // and an opaque buffer makes our temporary BMP deterministic.
                for (int x = 0;
                     x < width;
                     ++x)
                {
                    destinationRow[
                        static_cast<std::size_t>(x) * 4u + 3u]
                        = 0xFFu;
                }
            }

            return result;
        }


        void appendPageText(
            ExtractedPdfDocument& result,
            const int pageIndex,
            const int pageCount,
            const std::string_view sourceLabel,
            const std::string_view text,
            const std::size_t maximumBytes)
        {
            const std::string header =
                "\n--- PDF PAGE "
                + std::to_string(pageIndex + 1)
                + " OF "
                + std::to_string(pageCount)
                + " ["
                + std::string{ sourceLabel }
                + "] ---\n";

            appendWithLimit(
                result.text,
                header,
                maximumBytes,
                result.truncated);

            appendWithLimit(
                result.text,
                text,
                maximumBytes,
                result.truncated);

            appendWithLimit(
                result.text,
                "\n",
                maximumBytes,
                result.truncated);
        }

    } // namespace


    PdfTextExtractor::PdfTextExtractor(
        const PdfTextExtractorConfig config)
        : config_{ config }
    {
        if (config_.maximumExtractedUtf8Bytes == 0)
        {
            throw std::invalid_argument{
                "PdfTextExtractor maximumExtractedUtf8Bytes must be greater than zero."
            };
        }

        if (config_.maximumOcrPages == 0)
        {
            throw std::invalid_argument{
                "PdfTextExtractor maximumOcrPages must be greater than zero."
            };
        }

        if (config_.ocrRenderDpi <= 0.0f)
        {
            throw std::invalid_argument{
                "PdfTextExtractor OCR DPI must be greater than zero."
            };
        }

        if (config_.maximumOcrImageDimension <= 0)
        {
            throw std::invalid_argument{
                "PdfTextExtractor maximum OCR image dimension must be positive."
            };
        }

        FPDF_InitLibrary();
    }


    PdfTextExtractor::~PdfTextExtractor()
    {
        FPDF_DestroyLibrary();
    }


    ExtractedPdfDocument PdfTextExtractor::extract(
        const ReadBinaryFileResult& file,
        ocr::IOcrEngine& ocrEngine) const
    {
        if (file.bytes.empty())
        {
            throw std::runtime_error{
                "The PDF attachment is empty: "
                + file.displayName
            };
        }

        FPDF_DOCUMENT rawDocument =
            FPDF_LoadMemDocument64(
                file.bytes.data(),
                file.bytes.size(),
                nullptr);

        if (rawDocument == nullptr)
        {
            const unsigned long error =
                FPDF_GetLastError();

            throw std::runtime_error{
                "Could not open PDF '"
                + file.displayName
                + "': "
                + pdfiumErrorDescription(error)
            };
        }

        DocumentPtr document{
            rawDocument
        };

        const int pageCount =
            FPDF_GetPageCount(
                document.get());

        if (pageCount <= 0)
        {
            throw std::runtime_error{
                "The PDF contains no readable pages: "
                + file.displayName
            };
        }

        ExtractedPdfDocument result;
        result.pageCount = pageCount;

        for (int pageIndex = 0;
             pageIndex < pageCount;
             ++pageIndex)
        {
            if (result.truncated)
            {
                break;
            }

            FPDF_PAGE rawPage =
                FPDF_LoadPage(
                    document.get(),
                    pageIndex);

            if (rawPage == nullptr)
            {
                throw std::runtime_error{
                    "Could not load page "
                    + std::to_string(pageIndex + 1)
                    + " of PDF '"
                    + file.displayName
                    + "'."
                };
            }

            PagePtr page{
                rawPage
            };

            const std::string embeddedText =
                extractEmbeddedPageText(
                    page.get());

            const std::size_t embeddedCharacters =
                countNonWhitespace(
                    embeddedText);

            if (
                embeddedCharacters
                >= config_.minimumEmbeddedNonWhitespaceCharacters)
            {
                ++result.pagesWithText;
                ++result.pagesWithEmbeddedText;

                appendPageText(
                    result,
                    pageIndex,
                    pageCount,
                    "embedded text",
                    embeddedText,
                    config_.maximumExtractedUtf8Bytes);

                continue;
            }

            // The page has no meaningful text layer. This is the scanned-PDF path.
            if (!ocrEngine.available())
            {
                result.requiresOcr = true;

                if (containsNonWhitespace(embeddedText))
                {
                    ++result.pagesWithText;
                    ++result.pagesWithEmbeddedText;

                    appendPageText(
                        result,
                        pageIndex,
                        pageCount,
                        "limited embedded text; OCR unavailable",
                        embeddedText,
                        config_.maximumExtractedUtf8Bytes);
                }
                else
                {
                    ++result.pagesWithoutText;
                }

                continue;
            }

            if (
                static_cast<std::size_t>(result.pagesOcred)
                >= config_.maximumOcrPages)
            {
                // This is an intentional work bound, not a silent success.
                result.truncated = true;
                break;
            }

            const ocr::OcrBitmap raster =
                renderPageForOcr(
                    page.get(),
                    config_);

            ocr::OcrResult ocrResult;

            try
            {
                ocrResult =
                    ocrEngine.recognizeBitmap(
                        raster);
            }
            catch (const std::exception& exception)
            {
                throw std::runtime_error{
                    "OCR failed on page "
                    + std::to_string(pageIndex + 1)
                    + " of PDF '"
                    + file.displayName
                    + "': "
                    + exception.what()
                };
            }

            ++result.pagesOcred;

            if (containsNonWhitespace(ocrResult.text))
            {
                ++result.pagesWithText;

                appendPageText(
                    result,
                    pageIndex,
                    pageCount,
                    "OCR",
                    ocrResult.text,
                    config_.maximumExtractedUtf8Bytes);
            }
            else if (containsNonWhitespace(embeddedText))
            {
                // OCR found nothing useful, but preserve the small embedded layer
                // rather than discarding information that was already present.
                ++result.pagesWithText;
                ++result.pagesWithEmbeddedText;

                appendPageText(
                    result,
                    pageIndex,
                    pageCount,
                    "limited embedded text; OCR empty",
                    embeddedText,
                    config_.maximumExtractedUtf8Bytes);
            }
            else
            {
                ++result.pagesWithoutText;
            }
        }

        return result;
    }

} // namespace rose::tools
