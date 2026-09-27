#include "tools/CreatePdfDocumentTool.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rose::tools
{
    namespace
    {
        const std::string& required(const ToolRequest& request, const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            if (found == request.arguments.end() || found->second.empty())
                throw std::invalid_argument{ "create_pdf_document requires argument '" + std::string{ name } + "'." };
            return found->second;
        }

        std::string optional(const ToolRequest& request, const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            return found == request.arguments.end() ? std::string{} : found->second;
        }

        std::string decodeTextEscapes(const std::string_view encoded)
        {
            std::string decoded;
            decoded.reserve(encoded.size());
            for (std::size_t i = 0; i < encoded.size(); ++i)
            {
                if (encoded[i] == '\\' && i + 1 < encoded.size())
                {
                    const char next = encoded[i + 1];
                    if (next == 'n') { decoded.push_back('\n'); ++i; continue; }
                    if (next == 'r') { decoded.push_back('\r'); ++i; continue; }
                    if (next == 't') { decoded.push_back('\t'); ++i; continue; }
                    if (next == '\\') { decoded.push_back('\\'); ++i; continue; }
                }
                decoded.push_back(encoded[i]);
            }
            return decoded;
        }
    }

    CreatePdfDocumentTool::CreatePdfDocumentTool(documents::IPdfDocumentMutationService& service)
        : service_{ service }
        , descriptor_{
            .id = "create_pdf_document",
            .displayName = "Create PDF Document",
            .description = "Create one NEW PDF at an explicit absolute path using Rose's local PDFium backend. Existing files are never overwritten. The inverse is recycle_path.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "path", .description = "Absolute path of the NEW .pdf file.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "text", .description = "Optional initial text. Encode line breaks as literal \\n sequences.", .type = ToolValueType::String, .required = false }
            }
        }
    {
    }

    const ToolDescriptor& CreatePdfDocumentTool::descriptor() const noexcept { return descriptor_; }

    ToolResult CreatePdfDocumentTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id) throw std::invalid_argument{ "CreatePdfDocumentTool received a different tool id." };
        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "path" && name != "text") throw std::invalid_argument{ "create_pdf_document does not accept argument '" + name + "'." };
        }
        const std::filesystem::path path{ required(request, "path") };
        if (!path.is_absolute()) throw std::invalid_argument{ "create_pdf_document requires an absolute destination path." };
        const auto result = service_.create({ .path = path, .text = decodeTextEscapes(optional(request, "text")) });
        return ToolResult{
            .success = true,
            .message = "Created PDF document:\npath=" + path.lexically_normal().string()
                + "\noperation=" + result.operation
                + "\naffected_count=" + std::to_string(result.affectedCount)
                + "\nThe inverse is recycle_path if you want Rose to remove the newly created PDF.",
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
