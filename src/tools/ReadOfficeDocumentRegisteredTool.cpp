#include "tools/ReadOfficeDocumentRegisteredTool.h"

#include "files/FileFormatCatalog.h"
#include "model/IModelProvider.h"
#include "permissions/PermissionSystem.h"
#include "tools/ReadFileTool.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]]
        const std::string& pathArgument(
            const ToolRequest& request)
        {
            const auto found = request.arguments.find("path");
            if (found == request.arguments.end() || found->second.empty())
            {
                throw std::invalid_argument{
                    "read_office_document requires argument 'path'."
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
                        "read_office_document does not accept argument '"
                        + name + "'."
                    };
                }
            }
        }


        [[nodiscard]]
        std::string flattenOfficeDocument(
            const documents::ExtractedOpenXmlDocument& office)
        {
            std::ostringstream content;

            for (const auto& segment : office.segments)
            {
                content
                    << "\n--- "
                    << segment.locator
                    << " ---\n"
                    << segment.text;
            }

            return content.str();
        }
    }


    ReadOfficeDocumentRegisteredTool::ReadOfficeDocumentRegisteredTool(
        permissions::PermissionSystem& permissions,
        ReadFileTool& readFileTool,
        model::IModelProvider& modelProvider)
        : permissions_{ permissions }
        , readFileTool_{ readFileTool }
        , synthesizer_{ modelProvider }
        , descriptor_{
            .id = "read_office_document",
            .displayName = "Read Office Document",
            .description =
                "Read one exact modern Microsoft Office Open XML document "
                "(.docx/.docm, .xlsx/.xlsm, .pptx/.pptm). Word text, PowerPoint "
                "slides, and Excel sheet/cell values are extracted read-only. "
                "Large documents are summarized hierarchically so they cannot "
                "overflow Rose's bounded model context. Macros and embedded "
                "objects are never executed.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description = "Absolute path of the exact Office document to read.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "instruction",
                    .description =
                        "Optional analysis question or summarization instruction to "
                        "guide context-safe processing of large documents.",
                    .type = ToolValueType::String,
                    .required = false
                }
            }
        }
    {
    }


    const ToolDescriptor& ReadOfficeDocumentRegisteredTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult ReadOfficeDocumentRegisteredTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "ReadOfficeDocumentRegisteredTool received a different tool id."
            };
        }

        rejectUnknownArguments(request);

        const std::filesystem::path path{
            pathArgument(request)
        };

        if (!path.is_absolute())
        {
            throw std::invalid_argument{
                "read_office_document requires an absolute file path."
            };
        }

        if (!files::isOpenXmlOfficeFile(path))
        {
            throw std::invalid_argument{
                "read_office_document supports .docx/.docm, .xlsx/.xlsm, and .pptx/.pptm."
            };
        }

        permissions_.grantReadOnce(path);

        const ReadBinaryFileResult file =
            readFileTool_.readBinaryFile(path);

        const documents::ExtractedOpenXmlDocument office =
            extractor_.extract(
                file.bytes,
                path.extension().string());

        if (office.segments.empty())
        {
            throw std::runtime_error{
                "The Office document contained no extractable text/cell content."
            };
        }

        const std::string extracted =
            flattenOfficeDocument(office);

        const documents::ContextSafeDocumentSynthesisResult synthesis =
            synthesizer_.synthesize(
                extracted,
                office.contentKind,
                instructionArgument(request));

        std::string message =
            "Read Office document: " + file.path.string()
            + "\ncontent_kind=" + office.contentKind
            + "\nsegments=" + std::to_string(office.segments.size())
            + "\nsource_bytes=" + std::to_string(synthesis.sourceBytes)
            + "\nprocessed_bytes=" + std::to_string(synthesis.processedBytes)
            + "\nsource_truncated=" + std::string{ synthesis.sourceTruncated ? "true" : "false" }
            + "\ncoverage=" + std::string{ synthesis.sourceTruncated ? "bounded_prefix_only" : "full_extracted_content" }
            + "\ncontent_mode=" + std::string{ synthesis.synthesized ? "hierarchical_summary" : "raw" }
            + "\nanalysis_chunks=" + std::to_string(synthesis.chunkCount)
            + "\n<rose_untrusted_office_content>\n"
            + synthesis.text
            + "\n</rose_untrusted_office_content>";

        return ToolResult{
            .success = true,
            .message = std::move(message),
            .artifacts = {}
        };
    }
}
