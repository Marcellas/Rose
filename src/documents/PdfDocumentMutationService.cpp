#include "documents/PdfDocumentMutationService.h"

#if defined(ROSE_HAS_PDFIUM_MUTATION)
#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>
#include <fpdfview.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace rose::documents
{
    namespace
    {
        [[maybe_unused, noreturn]] void throwPdfiumUnavailable()
        {
            throw std::runtime_error{
                "PDF mutation is unavailable in this Rose build because the optional PDFium package is not installed."
            };
        }

        // Convert PDFium's signed count return values to a safe size_t without
        // using std::max. Windows headers may define max as a macro, which can
        // corrupt qualified std::max(...) calls even when <algorithm> is used.
        [[nodiscard]] constexpr std::size_t nonNegativeSize(const int value) noexcept
        {
            return value > 0 ? static_cast<std::size_t>(value) : std::size_t{ 0 };
        }

        void requirePdfPath(const std::filesystem::path& path, const bool mustExist)
        {
            if (!path.is_absolute())
            {
                throw std::invalid_argument{ "PDF mutation requires an absolute path." };
            }
            if (path.extension() != ".pdf" && path.extension() != ".PDF")
            {
                throw std::invalid_argument{ "PDF mutation accepts only .pdf files." };
            }
            if (mustExist && (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)))
            {
                throw std::runtime_error{ "PDF file does not exist or is not a regular file: " + path.string() };
            }
        }

        [[maybe_unused]] std::filesystem::path siblingTemporaryPath(const std::filesystem::path& path, const std::string_view tag)
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                const auto candidate = path.parent_path()
                    / (path.filename().string() + ".rose-" + std::string{ tag } + "-"
                        + std::to_string(stamp) + "-" + std::to_string(attempt) + ".tmp");
                if (!std::filesystem::exists(candidate))
                {
                    return candidate;
                }
            }
            throw std::runtime_error{ "Could not allocate a temporary PDF path." };
        }

        [[maybe_unused]] void transactionalReplace(const std::filesystem::path& original, const std::filesystem::path& replacement)
        {
            const auto backup = siblingTemporaryPath(original, "backup");
            std::error_code error;
            std::filesystem::rename(original, backup, error);
            if (error)
            {
                throw std::runtime_error{ "Could not stage original PDF for replacement: " + error.message() };
            }

            std::filesystem::rename(replacement, original, error);
            if (error)
            {
                std::error_code rollbackError;
                std::filesystem::rename(backup, original, rollbackError);
                throw std::runtime_error{ "Could not replace original PDF: " + error.message() };
            }

            std::filesystem::remove(backup, error);
        }

#if defined(ROSE_HAS_PDFIUM_MUTATION)
        struct DocumentCloser
        {
            void operator()(FPDF_DOCUMENT document) const noexcept
            {
                if (document) FPDF_CloseDocument(document);
            }
        };
        struct PageCloser
        {
            void operator()(FPDF_PAGE page) const noexcept
            {
                if (page) FPDF_ClosePage(page);
            }
        };
        struct AnnotationCloser
        {
            void operator()(FPDF_ANNOTATION annotation) const noexcept
            {
                if (annotation) FPDFPage_CloseAnnot(annotation);
            }
        };
        struct PageObjectCloser
        {
            void operator()(FPDF_PAGEOBJECT object) const noexcept
            {
                if (object) FPDFPageObj_Destroy(object);
            }
        };

        using DocumentPtr = std::unique_ptr<std::remove_pointer_t<FPDF_DOCUMENT>, DocumentCloser>;
        using PagePtr = std::unique_ptr<std::remove_pointer_t<FPDF_PAGE>, PageCloser>;
        using AnnotationPtr = std::unique_ptr<std::remove_pointer_t<FPDF_ANNOTATION>, AnnotationCloser>;
        using PageObjectPtr = std::unique_ptr<std::remove_pointer_t<FPDF_PAGEOBJECT>, PageObjectCloser>;

        std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path)
        {
            std::ifstream input{ path, std::ios::binary };
            if (!input)
            {
                throw std::runtime_error{ "Could not open PDF for reading: " + path.string() };
            }
            return std::vector<std::uint8_t>{
                std::istreambuf_iterator<char>{ input },
                std::istreambuf_iterator<char>{}
            };
        }

        DocumentPtr loadDocument(const std::filesystem::path& path, std::vector<std::uint8_t>& backingBytes)
        {
            backingBytes = readFileBytes(path);
            if (backingBytes.empty())
            {
                throw std::runtime_error{ "PDF file is empty: " + path.string() };
            }
            FPDF_DOCUMENT raw = FPDF_LoadMemDocument64(backingBytes.data(), backingBytes.size(), nullptr);
            if (!raw)
            {
                throw std::runtime_error{ "PDFium could not open PDF: " + path.string() };
            }
            return DocumentPtr{ raw };
        }

        struct FileWriter
        {
            FPDF_FILEWRITE api{};
            std::ofstream stream;
        };

        int writeBlock(FPDF_FILEWRITE* base, const void* data, unsigned long size)
        {
            auto* writer = reinterpret_cast<FileWriter*>(base);
            writer->stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            return writer->stream.good() ? 1 : 0;
        }

        void saveDocument(FPDF_DOCUMENT document, const std::filesystem::path& path)
        {
            FileWriter writer;
            writer.api.version = 1;
            writer.api.WriteBlock = &writeBlock;
            writer.stream.open(path, std::ios::binary | std::ios::trunc);
            if (!writer.stream)
            {
                throw std::runtime_error{ "Could not open temporary PDF for writing: " + path.string() };
            }
            if (!FPDF_SaveAsCopy(document, &writer.api, FPDF_NO_INCREMENTAL))
            {
                throw std::runtime_error{ "PDFium failed to save the PDF." };
            }
            writer.stream.flush();
            if (!writer.stream)
            {
                throw std::runtime_error{ "Writing the PDF failed before completion." };
            }
        }

        void appendUtf16(std::vector<unsigned short>& output, std::uint32_t codePoint)
        {
            if (codePoint <= 0xFFFFu)
            {
                if (codePoint >= 0xD800u && codePoint <= 0xDFFFu) codePoint = 0xFFFDu;
                output.push_back(static_cast<unsigned short>(codePoint));
                return;
            }
            if (codePoint > 0x10FFFFu) codePoint = 0xFFFDu;
            codePoint -= 0x10000u;
            output.push_back(static_cast<unsigned short>(0xD800u + (codePoint >> 10u)));
            output.push_back(static_cast<unsigned short>(0xDC00u + (codePoint & 0x3FFu)));
        }

        std::vector<unsigned short> utf8ToPdfWide(const std::string_view text)
        {
            std::vector<unsigned short> result;
            result.reserve(text.size() + 1);
            for (std::size_t i = 0; i < text.size();)
            {
                const auto c = static_cast<unsigned char>(text[i]);
                std::uint32_t cp = 0xFFFDu;
                std::size_t length = 1;
                if (c < 0x80u) { cp = c; length = 1; }
                else if ((c & 0xE0u) == 0xC0u && i + 1 < text.size())
                {
                    cp = ((c & 0x1Fu) << 6u) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu); length = 2;
                }
                else if ((c & 0xF0u) == 0xE0u && i + 2 < text.size())
                {
                    cp = ((c & 0x0Fu) << 12u)
                        | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6u)
                        | (static_cast<unsigned char>(text[i + 2]) & 0x3Fu); length = 3;
                }
                else if ((c & 0xF8u) == 0xF0u && i + 3 < text.size())
                {
                    cp = ((c & 0x07u) << 18u)
                        | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12u)
                        | ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6u)
                        | (static_cast<unsigned char>(text[i + 3]) & 0x3Fu); length = 4;
                }
                appendUtf16(result, cp);
                i += length;
            }
            result.push_back(0);
            return result;
        }

        std::vector<std::string> wrapText(const std::string& text, const std::size_t columns = 82)
        {
            std::vector<std::string> lines;
            std::string current;
            std::string word;
            auto flushWord = [&]
            {
                if (word.empty()) return;
                if (!current.empty() && current.size() + 1 + word.size() > columns)
                {
                    lines.push_back(current);
                    current.clear();
                }
                if (!current.empty()) current.push_back(' ');
                current += word;
                word.clear();
            };
            for (char c : text)
            {
                if (c == '\n')
                {
                    flushWord();
                    lines.push_back(current);
                    current.clear();
                }
                else if (c == ' ' || c == '\t' || c == '\r')
                {
                    flushWord();
                }
                else
                {
                    word.push_back(c);
                }
            }
            flushWord();
            if (!current.empty() || lines.empty()) lines.push_back(current);
            return lines;
        }

        std::size_t appendTextObjects(FPDF_DOCUMENT document, FPDF_PAGE page,
                                      const std::string& text, double x, double y, double fontSize)
        {
            if (text.empty()) throw std::invalid_argument{ "PDF text cannot be empty." };
            if (fontSize <= 0.0 || fontSize > 144.0) throw std::invalid_argument{ "PDF font_size must be between 0 and 144." };

            std::size_t inserted = 0;
            const double lineHeight = fontSize * 1.35;
            for (const std::string& line : wrapText(text))
            {
                if (y < 24.0) break;
                if (!line.empty())
                {
                    FPDF_PAGEOBJECT rawObject = FPDFPageObj_NewTextObj(document, "Helvetica", static_cast<float>(fontSize));
                    if (!rawObject) throw std::runtime_error{ "PDFium could not create a text object." };
                    PageObjectPtr object{ rawObject };
                    const auto wide = utf8ToPdfWide(line);
                    if (!FPDFText_SetText(object.get(), wide.data()))
                    {
                        throw std::runtime_error{ "PDFium could not set text on a PDF text object." };
                    }
                    FPDFPageObj_Transform(object.get(), 1.0, 0.0, 0.0, 1.0, x, y);
                    FPDFPage_InsertObject(page, object.release());
                    ++inserted;
                }
                y -= lineHeight;
            }
            if (!FPDFPage_GenerateContent(page))
            {
                throw std::runtime_error{ "PDFium could not regenerate page content after text insertion." };
            }
            return inserted;
        }

        std::size_t appendTextPage(FPDF_DOCUMENT document, const std::string& text)
        {
            const int index = FPDF_GetPageCount(document);
            FPDF_PAGE rawPage = FPDFPage_New(document, index, 612.0, 792.0); // US Letter points.
            if (!rawPage) throw std::runtime_error{ "PDFium could not create a PDF page." };
            PagePtr page{ rawPage };
            appendTextObjects(document, page.get(), text.empty() ? std::string{ " " } : text, 72.0, 720.0, 12.0);
            return 1;
        }

        PagePtr loadPage1Based(FPDF_DOCUMENT document, const std::size_t pageIndex)
        {
            const int count = FPDF_GetPageCount(document);
            if (pageIndex == 0 || pageIndex > nonNegativeSize(count))
            {
                throw std::out_of_range{ "PDF page index is outside the document." };
            }
            FPDF_PAGE raw = FPDF_LoadPage(document, static_cast<int>(pageIndex - 1));
            if (!raw) throw std::runtime_error{ "PDFium could not load the requested page." };
            return PagePtr{ raw };
        }

        void validateRange(const std::size_t start, const std::size_t end, const int pageCount)
        {
            if (start == 0 || end == 0 || start > end || end > nonNegativeSize(pageCount))
            {
                throw std::out_of_range{ "PDF page range is outside the document." };
            }
        }
#endif
    }

    LocalPdfDocumentMutationService::LocalPdfDocumentMutationService() = default;
    LocalPdfDocumentMutationService::~LocalPdfDocumentMutationService() = default;

    PdfMutationResult LocalPdfDocumentMutationService::create(const CreatePdfDocumentRequest& request)
    {
        requirePdfPath(request.path, false);
        if (std::filesystem::exists(request.path))
        {
            throw std::runtime_error{ "Rose will not overwrite an existing PDF: " + request.path.string() };
        }
        if (!std::filesystem::exists(request.path.parent_path()))
        {
            throw std::runtime_error{ "PDF destination parent directory does not exist: " + request.path.parent_path().string() };
        }
#if !defined(ROSE_HAS_PDFIUM_MUTATION)
        (void)request;
        throwPdfiumUnavailable();
#else
        DocumentPtr document{ FPDF_CreateNewDocument() };
        if (!document) throw std::runtime_error{ "PDFium could not create a PDF document." };
        appendTextPage(document.get(), request.text.empty() ? std::string{ " " } : request.text);
        const auto temp = siblingTemporaryPath(request.path, "create");
        try
        {
            saveDocument(document.get(), temp);
            std::error_code error;
            std::filesystem::rename(temp, request.path, error);
            if (error) throw std::runtime_error{ "Could not install the new PDF: " + error.message() };
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw;
        }
        return { "create_pdf_document", "Created one-page PDF.", 1 };
#endif
    }

    PdfMutationResult LocalPdfDocumentMutationService::edit(const EditPdfDocumentRequest& request)
    {
        requirePdfPath(request.path, true);
#if !defined(ROSE_HAS_PDFIUM_MUTATION)
        (void)request;
        throwPdfiumUnavailable();
#else
        std::vector<std::uint8_t> bytes;
        DocumentPtr document = loadDocument(request.path, bytes);
        const int originalPageCount = FPDF_GetPageCount(document.get());
        if (originalPageCount <= 0) throw std::runtime_error{ "PDF contains no pages." };

        std::size_t affected = 0;
        std::string detail;
        switch (request.kind)
        {
        case PdfMutationKind::AppendTextPage:
            affected = appendTextPage(document.get(), request.text);
            detail = "Appended text page.";
            break;

        case PdfMutationKind::RemovePageRange:
            validateRange(request.pageStart, request.pageEnd, originalPageCount);
            if ((request.pageEnd - request.pageStart + 1) >= static_cast<std::size_t>(originalPageCount))
            {
                throw std::invalid_argument{ "Rose will not remove every page from a PDF; recycle the document instead." };
            }
            for (std::size_t page = request.pageEnd; page >= request.pageStart; --page)
            {
                FPDFPage_Delete(document.get(), static_cast<int>(page - 1));
                ++affected;
                if (page == request.pageStart) break;
            }
            detail = "Removed page range.";
            break;

        case PdfMutationKind::RotatePage:
        {
            if (request.rotationDegrees != 90 && request.rotationDegrees != 180 && request.rotationDegrees != 270)
            {
                throw std::invalid_argument{ "rotation_degrees must be 90, 180, or 270 clockwise." };
            }
            PagePtr page = loadPage1Based(document.get(), request.pageIndex);
            const int current = FPDFPage_GetRotation(page.get());
            const int delta = request.rotationDegrees / 90;
            FPDFPage_SetRotation(page.get(), (current + delta) % 4);
            affected = 1;
            detail = "Rotated page clockwise.";
            break;
        }

        case PdfMutationKind::AddTextToPage:
        {
            PagePtr page = loadPage1Based(document.get(), request.pageIndex);
            affected = appendTextObjects(document.get(), page.get(), request.text, request.x, request.y, request.fontSize);
            detail = "Added PDF text object(s). first_new_object_index="
                + std::to_string(static_cast<std::size_t>(FPDFPage_CountObjects(page.get())) - affected + 1);
            break;
        }

        case PdfMutationKind::RemovePageObject:
        {
            PagePtr page = loadPage1Based(document.get(), request.pageIndex);
            const int count = FPDFPage_CountObjects(page.get());
            if (request.objectIndex == 0 || request.objectIndex > nonNegativeSize(count))
                throw std::out_of_range{ "PDF page object index is outside the page." };
            FPDF_PAGEOBJECT object = FPDFPage_GetObject(page.get(), static_cast<int>(request.objectIndex - 1));
            if (!object || !FPDFPage_RemoveObject(page.get(), object))
                throw std::runtime_error{ "PDFium could not remove the requested page object." };
            PageObjectPtr removed{ object };
            if (!FPDFPage_GenerateContent(page.get()))
                throw std::runtime_error{ "PDFium could not regenerate page content after object removal." };
            affected = 1;
            detail = "Removed page object.";
            break;
        }

        case PdfMutationKind::AddTextAnnotation:
        {
            PagePtr page = loadPage1Based(document.get(), request.pageIndex);
            FPDF_ANNOTATION rawAnnotation = FPDFPage_CreateAnnot(page.get(), FPDF_ANNOT_TEXT);
            if (!rawAnnotation) throw std::runtime_error{ "PDFium could not create a text annotation." };
            AnnotationPtr annotation{ rawAnnotation };
            const auto wide = utf8ToPdfWide(request.text);
            if (!FPDFAnnot_SetStringValue(annotation.get(), "Contents", wide.data()))
                throw std::runtime_error{ "PDFium could not set annotation contents." };
            FS_RECTF rect{
                static_cast<float>(request.x),
                static_cast<float>(request.y + 24.0),
                static_cast<float>(request.x + 24.0),
                static_cast<float>(request.y)
            };
            if (!FPDFAnnot_SetRect(annotation.get(), &rect))
                throw std::runtime_error{ "PDFium could not position the annotation." };
            affected = 1;
            detail = "Added text annotation. annotation_index="
                + std::to_string(FPDFPage_GetAnnotCount(page.get()));
            break;
        }

        case PdfMutationKind::RemoveAnnotation:
        {
            PagePtr page = loadPage1Based(document.get(), request.pageIndex);
            const int count = FPDFPage_GetAnnotCount(page.get());
            if (request.annotationIndex == 0 || request.annotationIndex > nonNegativeSize(count))
                throw std::out_of_range{ "PDF annotation index is outside the page." };
            if (!FPDFPage_RemoveAnnot(page.get(), static_cast<int>(request.annotationIndex - 1)))
                throw std::runtime_error{ "PDFium could not remove the requested annotation." };
            affected = 1;
            detail = "Removed annotation.";
            break;
        }

        case PdfMutationKind::AppendPdfPages:
        {
            requirePdfPath(request.sourcePath, true);
            if (std::filesystem::equivalent(request.path, request.sourcePath))
                throw std::invalid_argument{ "append_pdf_pages source must be a different PDF." };
            std::vector<std::uint8_t> sourceBytes;
            DocumentPtr source = loadDocument(request.sourcePath, sourceBytes);
            const int sourceCount = FPDF_GetPageCount(source.get());
            if (sourceCount <= 0) throw std::runtime_error{ "Source PDF contains no pages." };
            const std::string range = request.pageRange.empty() ? std::string{} : request.pageRange;
            if (!FPDF_ImportPages(document.get(), source.get(), range.empty() ? nullptr : range.c_str(), originalPageCount))
                throw std::runtime_error{ "PDFium could not import the requested source pages." };
            const int after = FPDF_GetPageCount(document.get());
            affected = nonNegativeSize(after - originalPageCount);
            detail = "Appended pages from source PDF. appended_page_start="
                + std::to_string(originalPageCount + 1) + " appended_page_end=" + std::to_string(after);
            break;
        }
        }

        const auto temp = siblingTemporaryPath(request.path, "edit");
        try
        {
            saveDocument(document.get(), temp);
            transactionalReplace(request.path, temp);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw;
        }

        return { "edit_pdf_document", std::move(detail), affected };
#endif
    }

    PdfMutationResult LocalPdfDocumentMutationService::extract(const ExtractPdfPagesRequest& request)
    {
        requirePdfPath(request.sourcePath, true);
        requirePdfPath(request.destinationPath, false);
        if (request.pageRange.empty()) throw std::invalid_argument{ "extract_pdf_pages requires a page range such as 1,3,5-7." };
        if (std::filesystem::exists(request.destinationPath))
            throw std::runtime_error{ "Rose will not overwrite an existing PDF: " + request.destinationPath.string() };
#if !defined(ROSE_HAS_PDFIUM_MUTATION)
        (void)request;
        throwPdfiumUnavailable();
#else
        std::vector<std::uint8_t> sourceBytes;
        DocumentPtr source = loadDocument(request.sourcePath, sourceBytes);
        DocumentPtr destination{ FPDF_CreateNewDocument() };
        if (!destination) throw std::runtime_error{ "PDFium could not create the destination PDF." };
        if (!FPDF_ImportPages(destination.get(), source.get(), request.pageRange.c_str(), 0))
            throw std::runtime_error{ "PDFium could not extract the requested page range." };
        const int count = FPDF_GetPageCount(destination.get());
        if (count <= 0) throw std::runtime_error{ "The requested page range produced no pages." };

        const auto temp = siblingTemporaryPath(request.destinationPath, "extract");
        try
        {
            saveDocument(destination.get(), temp);
            std::error_code error;
            std::filesystem::rename(temp, request.destinationPath, error);
            if (error) throw std::runtime_error{ "Could not install extracted PDF: " + error.message() };
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw;
        }
        return { "extract_pdf_pages", "Created a new PDF from the requested page range.", static_cast<std::size_t>(count) };
#endif
    }
}
