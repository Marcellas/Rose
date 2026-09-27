#include "tools/ExtractPdfPagesTool.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace rose::tools
{
    ExtractPdfPagesTool::ExtractPdfPagesTool(documents::IPdfDocumentMutationService& service)
        : service_{ service }
        , descriptor_{
            .id = "extract_pdf_pages",
            .displayName = "Extract PDF Pages",
            .description = "Create one NEW PDF from an explicit page range of an existing PDF. The destination is never overwritten; inverse is recycle_path.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "source_path", .description = "Absolute path of the source PDF.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "destination_path", .description = "Absolute path of the NEW destination PDF.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "pages", .description = "Page range such as 1,3,5-7.", .type = ToolValueType::String, .required = true }
            }
        }
    {
    }

    const ToolDescriptor& ExtractPdfPagesTool::descriptor() const noexcept { return descriptor_; }

    ToolResult ExtractPdfPagesTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id) throw std::invalid_argument{ "ExtractPdfPagesTool received a different tool id." };
        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "source_path" && name != "destination_path" && name != "pages")
                throw std::invalid_argument{ "extract_pdf_pages does not accept argument '" + name + "'." };
        }
        const auto source = request.arguments.find("source_path");
        const auto destination = request.arguments.find("destination_path");
        const auto pages = request.arguments.find("pages");
        if (source == request.arguments.end() || source->second.empty()
            || destination == request.arguments.end() || destination->second.empty()
            || pages == request.arguments.end() || pages->second.empty())
            throw std::invalid_argument{ "extract_pdf_pages requires source_path, destination_path, and pages." };
        const std::filesystem::path sourcePath{ source->second };
        const std::filesystem::path destinationPath{ destination->second };
        if (!sourcePath.is_absolute() || !destinationPath.is_absolute())
            throw std::invalid_argument{ "extract_pdf_pages requires absolute source and destination paths." };

        const auto result = service_.extract({ .sourcePath = sourcePath, .destinationPath = destinationPath, .pageRange = pages->second });
        return ToolResult{
            .success = true,
            .message = "Extracted PDF pages:\nsource=" + sourcePath.lexically_normal().string()
                + "\ndestination=" + destinationPath.lexically_normal().string()
                + "\npages=" + pages->second
                + "\naffected_count=" + std::to_string(result.affectedCount)
                + "\nThe inverse is recycle_path for the newly created PDF.",
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
