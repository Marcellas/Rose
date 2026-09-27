#include "tools/EditPdfDocumentTool.h"

#include <algorithm>
#include <charconv>
#include <cctype>
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
                throw std::invalid_argument{ "edit_pdf_document requires argument '" + std::string{ name } + "'." };
            return found->second;
        }
        std::string optional(const ToolRequest& request, const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            return found == request.arguments.end() ? std::string{} : found->second;
        }
        std::string lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
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
        documents::PdfMutationKind parseOperation(const std::string_view raw)
        {
            const std::string value = lower(std::string{ raw });
            if (value == "append_text_page") return documents::PdfMutationKind::AppendTextPage;
            if (value == "remove_page_range") return documents::PdfMutationKind::RemovePageRange;
            if (value == "rotate_page") return documents::PdfMutationKind::RotatePage;
            if (value == "add_text_to_page") return documents::PdfMutationKind::AddTextToPage;
            if (value == "remove_page_object") return documents::PdfMutationKind::RemovePageObject;
            if (value == "add_text_annotation") return documents::PdfMutationKind::AddTextAnnotation;
            if (value == "remove_annotation") return documents::PdfMutationKind::RemoveAnnotation;
            if (value == "append_pdf_pages") return documents::PdfMutationKind::AppendPdfPages;
            throw std::invalid_argument{ "Unsupported edit_pdf_document operation." };
        }
        std::size_t parseSize(const std::string& raw, const std::string_view field)
        {
            if (raw.empty()) return 0;
            std::size_t value{};
            const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size())
                throw std::invalid_argument{ std::string{ field } + " must be a positive integer." };
            return value;
        }
        int parseInt(const std::string& raw, const std::string_view field)
        {
            if (raw.empty()) return 0;
            int value{};
            const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size())
                throw std::invalid_argument{ std::string{ field } + " must be an integer." };
            return value;
        }
        double parseDouble(const std::string& raw, const std::string_view field, const double fallback)
        {
            if (raw.empty()) return fallback;
            try { return std::stod(raw); }
            catch (...) { throw std::invalid_argument{ std::string{ field } + " must be numeric." }; }
        }
    }

    EditPdfDocumentTool::EditPdfDocumentTool(documents::IPdfDocumentMutationService& service)
        : service_{ service }
        , descriptor_{
            .id = "edit_pdf_document",
            .displayName = "Edit PDF Document",
            .description =
                "Apply one explicit mutation to an existing PDF transactionally. Operations: append_text_page/remove_page_range, rotate_page, "
                "add_text_to_page/remove_page_object, add_text_annotation/remove_annotation, and append_pdf_pages. Rose writes a temporary PDF and replaces the original only after success.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "path", .description = "Absolute path of the existing PDF.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "operation", .description = "Exact supported PDF mutation operation.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "text", .description = "Text for a new page, page text, or text annotation.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "page", .description = "1-based page index.", .type = ToolValueType::Integer, .required = false },
                ToolParameterDescriptor{ .name = "page_start", .description = "1-based inclusive start page for removal.", .type = ToolValueType::Integer, .required = false },
                ToolParameterDescriptor{ .name = "page_end", .description = "1-based inclusive end page for removal.", .type = ToolValueType::Integer, .required = false },
                ToolParameterDescriptor{ .name = "object_index", .description = "1-based page-object index returned/identified for removal.", .type = ToolValueType::Integer, .required = false },
                ToolParameterDescriptor{ .name = "annotation_index", .description = "1-based annotation index for removal.", .type = ToolValueType::Integer, .required = false },
                ToolParameterDescriptor{ .name = "rotation_degrees", .description = "Clockwise delta: 90, 180, or 270.", .type = ToolValueType::Integer, .required = false },
                ToolParameterDescriptor{ .name = "x", .description = "PDF point x coordinate; defaults to 72.", .type = ToolValueType::Number, .required = false },
                ToolParameterDescriptor{ .name = "y", .description = "PDF point y coordinate; defaults to 720.", .type = ToolValueType::Number, .required = false },
                ToolParameterDescriptor{ .name = "font_size", .description = "Text size in PDF points; defaults to 12.", .type = ToolValueType::Number, .required = false },
                ToolParameterDescriptor{ .name = "source_path", .description = "Absolute source PDF for append_pdf_pages.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "pages", .description = "Optional PDFium page-range string such as 1,3,5-7 for append_pdf_pages.", .type = ToolValueType::String, .required = false }
            }
        }
    {
    }

    const ToolDescriptor& EditPdfDocumentTool::descriptor() const noexcept { return descriptor_; }

    ToolResult EditPdfDocumentTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id) throw std::invalid_argument{ "EditPdfDocumentTool received a different tool id." };
        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "path" && name != "operation" && name != "text" && name != "page"
                && name != "page_start" && name != "page_end" && name != "object_index"
                && name != "annotation_index" && name != "rotation_degrees" && name != "x"
                && name != "y" && name != "font_size" && name != "source_path" && name != "pages")
                throw std::invalid_argument{ "edit_pdf_document does not accept argument '" + name + "'." };
        }

        const std::filesystem::path path{ required(request, "path") };
        if (!path.is_absolute()) throw std::invalid_argument{ "edit_pdf_document requires an absolute PDF path." };
        const std::string sourceRaw = optional(request, "source_path");
        const std::filesystem::path sourcePath = sourceRaw.empty() ? std::filesystem::path{} : std::filesystem::path{ sourceRaw };
        if (!sourcePath.empty() && !sourcePath.is_absolute()) throw std::invalid_argument{ "edit_pdf_document source_path must be absolute." };

        const auto result = service_.edit(documents::EditPdfDocumentRequest{
            .path = path,
            .kind = parseOperation(required(request, "operation")),
            .text = decodeTextEscapes(optional(request, "text")),
            .sourcePath = sourcePath,
            .pageRange = optional(request, "pages"),
            .pageIndex = parseSize(optional(request, "page"), "page"),
            .pageStart = parseSize(optional(request, "page_start"), "page_start"),
            .pageEnd = parseSize(optional(request, "page_end"), "page_end"),
            .objectIndex = parseSize(optional(request, "object_index"), "object_index"),
            .annotationIndex = parseSize(optional(request, "annotation_index"), "annotation_index"),
            .rotationDegrees = parseInt(optional(request, "rotation_degrees"), "rotation_degrees"),
            .x = parseDouble(optional(request, "x"), "x", 72.0),
            .y = parseDouble(optional(request, "y"), "y", 720.0),
            .fontSize = parseDouble(optional(request, "font_size"), "font_size", 12.0)
        });

        return ToolResult{
            .success = true,
            .message = "Edited PDF document:\npath=" + path.lexically_normal().string()
                + "\noperation=" + result.operation
                + "\naffected_count=" + std::to_string(result.affectedCount)
                + "\ndetail=" + result.detail
                + "\nProject Knowledge may need /knowledge index to refresh persisted PDF excerpts.",
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
