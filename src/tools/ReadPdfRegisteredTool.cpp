#include "tools/ReadPdfRegisteredTool.h"

#include "files/FileFormatCatalog.h"
#include "model/IModelProvider.h"
#include "ocr/IOcrEngine.h"
#include "permissions/PermissionSystem.h"
#include "tools/ReadFileTool.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace rose::tools
{
    namespace
    {
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
            return found == request.arguments.end()
                ? std::string_view{}
                : std::string_view{ found->second };
        }


        void rejectUnknownArguments(
            const ToolRequest& request)
        {
            for (const auto& [name, value] : request.arguments)
            {
                (void)value;

                if (name != "path" && name != "instruction")
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
            .maximumOcrPages = 16u,
            .ocrRenderDpi = 200.0f,
            .maximumOcrImageDimension = 3000
        } }
        , synthesizer_{ modelProvider }
        , descriptor_{
            .id = "read_pdf",
            .displayName = "Read PDF",
            .description =
                "Read one exact PDF at an absolute path using its embedded text "
                "layer with OCR fallback for scanned pages. Large PDFs are "
                "summarized hierarchically so they cannot overflow Rose's bounded "
                "model context. The operation is read-only.",
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

        permissions_.grantReadOnce(path);

        const ReadBinaryFileResult file =
            readFileTool_.readBinaryFile(path);

        const ExtractedPdfDocument pdf =
            extractor_.extract(
                file,
                *ocrEngine_);

        if (pdf.text.empty())
        {
            throw std::runtime_error{
                "The PDF contained no extractable or OCR-recognized text."
            };
        }

        const documents::ContextSafeDocumentSynthesisResult synthesis =
            synthesizer_.synthesize(
                pdf.text,
                "pdf",
                instructionArgument(request));

        std::string message =
            "Read PDF: " + file.path.string()
            + "\npages=" + std::to_string(pdf.pageCount)
            + "\npages_with_text=" + std::to_string(pdf.pagesWithText)
            + "\npages_ocr=" + std::to_string(pdf.pagesOcred)
            + "\nextractor_truncated=" + std::string{ pdf.truncated ? "true" : "false" }
            + "\nsource_bytes=" + std::to_string(synthesis.sourceBytes)
            + "\nprocessed_bytes=" + std::to_string(synthesis.processedBytes)
            + "\nsource_truncated=" + std::string{ synthesis.sourceTruncated ? "true" : "false" }
            + "\ncoverage=" + std::string{ synthesis.sourceTruncated ? "bounded_prefix_only" : "full_extracted_content" }
            + "\ncontent_mode=" + std::string{ synthesis.synthesized ? "hierarchical_summary" : "raw" }
            + "\nanalysis_chunks=" + std::to_string(synthesis.chunkCount)
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
