#include "tools/ReadPdfRegisteredTool.h"

#include "files/FileFormatCatalog.h"
#include "model/IModelProvider.h"
#include "ocr/IOcrEngine.h"
#include "permissions/PermissionSystem.h"
#include "tools/ReadFileTool.h"

#include <charconv>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace rose::tools
{
    namespace
    {
        constexpr std::size_t maximumPdfBinaryBytes{
            512u * 1024u * 1024u
        };


        const std::string& pathArgument(
            const ToolRequest& request)
        {
            const auto found = request.arguments.find("path");
            if (found == request.arguments.end() || found->second.empty())
            {
                throw std::invalid_argument{
                    "read_pdf requires argument 'path'."
                };
            }

            return found->second;
        }


        [[nodiscard]]
        std::string_view instructionArgument(
            const ToolRequest& request)
        {
            const auto found = request.arguments.find("instruction");
            // A whole multi-file request can contain many pages of directions.
            // The final response still receives it; the per-page worker needs
            // only a compact reading objective.
            return found == request.arguments.end()
                ? std::string_view{}
                : found->second.size() > 1024u
                    ? std::string_view{
                        "Preserve material facts, dates, page references, rules, "
                        "decisions, reasons, and limitations from this PDF." }
                    : std::string_view{ found->second };
        }


        [[nodiscard]]
        std::optional<std::size_t> optionalPositiveInteger(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(std::string{ name });

            if (found == request.arguments.end())
            {
                return std::nullopt;
            }

            std::size_t value{ 0 };
            const char* first = found->second.data();
            const char* last = first + found->second.size();
            const auto [end, error] =
                std::from_chars(first, last, value);

            if (error != std::errc{} || end != last || value == 0u)
            {
                throw std::invalid_argument{
                    "read_pdf argument '" + std::string{ name }
                    + "' must be a positive integer."
                };
            }

            return value;
        }


        [[nodiscard]]
        PdfPageSelection pageSelection(
            const ToolRequest& request)
        {
            const std::optional<std::size_t> start =
                optionalPositiveInteger(request, "page_start");
            const std::optional<std::size_t> count =
                optionalPositiveInteger(request, "page_count");

            return PdfPageSelection{
                .startPage = start.value_or(1u),
                .pageCount = count
            };
        }


        void rejectUnknownArguments(
            const ToolRequest& request)
        {
            for (const auto& [name, value] : request.arguments)
            {
                (void)value;

                if (name != "path" && name != "instruction"
                    && name != "page_start" && name != "page_count")
                {
                    throw std::invalid_argument{
                        "read_pdf does not accept argument '"
                        + name + "'."
                    };
                }
            }
        }
    }


    ReadPdfRegisteredTool::ReadPdfRegisteredTool(
        permissions::PermissionSystem& permissions,
        ReadFileTool& readFileTool,
        std::unique_ptr<ocr::IOcrEngine> ocrEngine,
        model::IModelProvider& modelProvider)
        : permissions_{ permissions }
        , readFileTool_{ readFileTool }
        , ocrEngine_{ std::move(ocrEngine) }
        , extractor_{ PdfTextExtractorConfig{
            .maximumExtractedUtf8Bytes = 512u * 1024u,
            .minimumEmbeddedNonWhitespaceCharacters = 24u,
            .maximumOcrPages = 32u,
            .ocrRenderDpi = 200.0f,
            .maximumOcrImageDimension = 3000
        } }
        , synthesizer_{ modelProvider }
        , descriptor_{
            .id = "read_pdf",
            .displayName = "Read PDF",
            .description =
                "Read one exact PDF at an absolute path using its embedded text "
                "layer with OCR fallback for scanned pages. Optional page_start/page_count "
                "limits extraction to a requested page window. Large PDFs are summarized "
                "hierarchically so they cannot overflow Rose's bounded model context. "
                "The operation is read-only.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description = "Absolute path of the exact PDF to read.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "instruction",
                    .description =
                        "Optional analysis question or summarization instruction to "
                        "guide context-safe processing of large PDFs.",
                    .type = ToolValueType::String,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "page_start",
                    .description =
                        "Optional one-based first page to read. Defaults to page 1.",
                    .type = ToolValueType::Integer,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "page_count",
                    .description =
                        "Optional positive number of pages to read from page_start. "
                        "When omitted, read through the end of the PDF.",
                    .type = ToolValueType::Integer,
                    .required = false
                }
            }
        }
    {
        if (!ocrEngine_)
        {
            throw std::invalid_argument{
                "ReadPdfRegisteredTool requires an OCR provider object."
            };
        }
    }


    ReadPdfRegisteredTool::~ReadPdfRegisteredTool() = default;


    const ToolDescriptor& ReadPdfRegisteredTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult ReadPdfRegisteredTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "ReadPdfRegisteredTool received a different tool id."
            };
        }

        rejectUnknownArguments(request);

        const std::filesystem::path path{
            pathArgument(request)
        };

        if (!path.is_absolute())
        {
            throw std::invalid_argument{
                "read_pdf requires an absolute file path."
            };
        }

        if (!files::isPdfFile(path))
        {
            throw std::invalid_argument{
                "read_pdf only accepts .pdf files."
            };
        }

        const PdfPageSelection selection =
            pageSelection(request);

        permissions_.grantReadOnce(path);

        // Each extraction is bounded, but the review advances through every
        // requested page window. Keep PDF backing bytes alive across windows;
        // PDFium opens one window at a time and never feeds the full source to
        // the language model in a single prompt.
        const ReadBinaryFileResult file = readFileTool_.readBinaryFile(
            path, maximumPdfBinaryBytes);
        constexpr std::size_t pagesPerWindow{ 32u };
        std::size_t nextPage = selection.startPage;
        std::size_t remaining = selection.pageCount.value_or(
            (std::numeric_limits<std::size_t>::max)());
        ExtractedPdfDocument pdf;
        documents::ContextSafeDocumentSynthesisResult synthesis;
        std::string windowNotes;
        std::size_t analysisWindows{ 0 };
        std::size_t analysisChunks{ 0 };
        bool incomplete{ false };
        std::string interruption;
        while (remaining > 0u)
        {
            std::size_t count = (std::min)(pagesPerWindow, remaining);
            ExtractedPdfDocument window;
            try
            {
                // A dense page or OCR bound may overflow even a 32-page
                // window. Halve and retry without skipping the affected page.
                while (true)
                {
                    window = extractor_.extract(file, *ocrEngine_,
                        PdfPageSelection{ .startPage = nextPage,
                            .pageCount = count });
                    if (!window.truncated || count == 1u) break;
                    count = (std::max)(std::size_t{ 1u }, count / 2u);
                }
                if (analysisWindows == 0u)
                {
                    pdf.pageCount = window.pageCount;
                    pdf.selectedPageStart = window.selectedPageStart;
                    const std::size_t available = static_cast<std::size_t>(
                        window.pageCount - window.selectedPageStart + 1);
                    const std::size_t requested = selection.pageCount.value_or(available);
                    pdf.selectedPageCount = static_cast<int>((std::min)(requested, available));
                    pdf.selectedPageEnd = pdf.selectedPageStart + pdf.selectedPageCount - 1;
                    remaining = static_cast<std::size_t>(pdf.selectedPageCount);
                }

                const auto part = synthesizer_.synthesize(
                    window.text, "pdf", instructionArgument(request));
                windowNotes += "\n--- PDF PAGES "
                    + std::to_string(window.selectedPageStart) + "-"
                    + std::to_string(window.selectedPageEnd) + " ---\n"
                    + (window.text.empty() ? "No extractable text."
                        : part.text) + "\n";
                synthesis.sourceBytes += part.sourceBytes;
                synthesis.processedBytes += part.processedBytes;
                synthesis.sourceTruncated |= part.sourceTruncated;
                synthesis.synthesized |= part.synthesized;
                analysisChunks += part.chunkCount;
                incomplete |= window.truncated || window.requiresOcr
                    || part.sourceTruncated;
                pdf.pagesExamined += window.pagesExamined;
                pdf.pagesWithText += window.pagesWithText;
                pdf.pagesOcred += window.pagesOcred;
                pdf.pagesWithoutText += window.pagesWithoutText;
                ++analysisWindows;
                remaining -= static_cast<std::size_t>(window.selectedPageCount);
                nextPage += static_cast<std::size_t>(window.selectedPageCount);
            }
            catch (const std::exception& exception)
            {
                if (analysisWindows == 0u) throw;
                incomplete = true;
                interruption = "Review stopped at page " + std::to_string(nextPage)
                    + ": " + exception.what();
                break;
            }
        }

        if (windowNotes.empty() || pdf.pagesWithText == 0)
            throw std::runtime_error{
                "The PDF contained no extractable or OCR-recognized text." };

        // Condense the page-labelled window notes only after every window was
        // visited. The source coverage counters refer to actual extraction,
        // not to the shorter final note used for conversational synthesis.
        if (analysisWindows == 1u)
        {
            synthesis.text = std::move(windowNotes);
        }
        else
        {
            const auto combined = synthesizer_.synthesize(windowNotes,
                "pdf page-window notes", instructionArgument(request));
            synthesis.text = combined.text;
            synthesis.synthesized = true;
            synthesis.sourceTruncated |= combined.sourceTruncated;
            incomplete |= combined.sourceTruncated;
        }
        synthesis.chunkCount = analysisChunks;
        pdf.truncated = incomplete;

        const bool pageWindowLimited =
            pdf.selectedPageStart != 1
            || pdf.selectedPageEnd != pdf.pageCount;

        std::string coverage;
        if (pdf.truncated || synthesis.sourceTruncated)
        {
            coverage = "partial_requested_page_window";
        }
        else if (pageWindowLimited)
        {
            coverage = "requested_page_window_complete";
        }
        else
        {
            coverage = "full_extracted_content";
        }

        std::string message =
            "Read PDF: " + path.string()
            + "\nsource_file_bytes=" + std::to_string(file.originalSize)
            + "\npages=" + std::to_string(pdf.pageCount)
            + "\nselected_page_start=" + std::to_string(pdf.selectedPageStart)
            + "\nselected_page_end=" + std::to_string(pdf.selectedPageEnd)
            + "\nselected_page_count=" + std::to_string(pdf.selectedPageCount)
            + "\npages_examined=" + std::to_string(pdf.pagesExamined)
            + "\npages_with_text=" + std::to_string(pdf.pagesWithText)
            + "\npages_ocr=" + std::to_string(pdf.pagesOcred)
            + "\nextractor_truncated=" + std::string{ pdf.truncated ? "true" : "false" }
            + "\nsource_bytes=" + std::to_string(synthesis.sourceBytes)
            + "\nprocessed_bytes=" + std::to_string(synthesis.processedBytes)
            + "\nsource_truncated=" + std::string{ synthesis.sourceTruncated ? "true" : "false" }
            + "\ncoverage=" + coverage
            + "\ncontent_mode=" + std::string{ synthesis.synthesized ? "hierarchical_summary" : "raw" }
            + "\nanalysis_chunks=" + std::to_string(synthesis.chunkCount)
            + "\nanalysis_windows=" + std::to_string(analysisWindows)
            + (interruption.empty() ? "" : "\n" + interruption)
            + "\n<rose_untrusted_pdf_content>\n"
            + synthesis.text
            + "\n</rose_untrusted_pdf_content>";

        return ToolResult{
            .success = true,
            .message = std::move(message),
            .artifacts = {}
        };
    }
}
