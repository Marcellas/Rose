#include "tools/EditOfficeDocumentTool.h"

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
        [[nodiscard]]
        const std::string& requiredArgument(const ToolRequest& request, const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            if (found == request.arguments.end() || found->second.empty())
            {
                throw std::invalid_argument{
                    "Tool '" + request.toolId + "' requires argument '" + std::string{ name } + "'."
                };
            }
            return found->second;
        }

        [[nodiscard]]
        std::string optionalArgument(const ToolRequest& request, const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            return found == request.arguments.end() ? std::string{} : found->second;
        }

        [[nodiscard]]
        std::string lowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        [[nodiscard]]
        std::string decodeTextEscapes(const std::string_view encoded)
        {
            std::string decoded;
            decoded.reserve(encoded.size());
            for (std::size_t i = 0; i < encoded.size(); ++i)
            {
                if (encoded[i] != '\\' || i + 1 >= encoded.size())
                {
                    decoded.push_back(encoded[i]);
                    continue;
                }
                const char next = encoded[i + 1];
                if (next == 'n') { decoded.push_back('\n'); ++i; }
                else if (next == 'r') { decoded.push_back('\r'); ++i; }
                else if (next == 't') { decoded.push_back('\t'); ++i; }
                else if (next == '\\') { decoded.push_back('\\'); ++i; }
                else { decoded.push_back('\\'); }
            }
            return decoded;
        }

        [[nodiscard]]
        documents::OfficeMutationKind parseOperation(const std::string_view raw)
        {
            const std::string value = lowerAscii(std::string{ raw });
            if (value == "append_word_text") return documents::OfficeMutationKind::AppendWordText;
            if (value == "remove_word_text") return documents::OfficeMutationKind::RemoveWordText;
            if (value == "replace_word_text") return documents::OfficeMutationKind::ReplaceWordText;
            if (value == "set_excel_cell") return documents::OfficeMutationKind::SetExcelCell;
            if (value == "clear_excel_cell") return documents::OfficeMutationKind::ClearExcelCell;
            if (value == "append_powerpoint_slide") return documents::OfficeMutationKind::AppendPowerPointSlide;
            if (value == "remove_powerpoint_slide") return documents::OfficeMutationKind::RemovePowerPointSlide;
            throw std::invalid_argument{
                "Unsupported edit_office_document operation."
            };
        }

        [[nodiscard]]
        std::size_t parseSlideIndex(const std::string& raw)
        {
            if (raw.empty()) return 0;
            std::size_t value{ 0 };
            const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size())
            {
                throw std::invalid_argument{ "slide_index must be a positive integer." };
            }
            return value;
        }

        void rejectUnknownArguments(const ToolRequest& request)
        {
            for (const auto& [name, value] : request.arguments)
            {
                (void)value;
                if (name != "path" && name != "operation" && name != "text"
                    && name != "find" && name != "replacement" && name != "sheet"
                    && name != "cell" && name != "slide_index")
                {
                    throw std::invalid_argument{
                        "Tool 'edit_office_document' does not accept argument '" + name + "'."
                    };
                }
            }
        }
    }

    EditOfficeDocumentTool::EditOfficeDocumentTool(
        documents::IOfficeDocumentMutationService& mutationService)
        : mutationService_{ mutationService }
        , descriptor_{
            .id = "edit_office_document",
            .displayName = "Edit Office Document",
            .description =
                "Apply one explicit mutation to an existing .docx/.xlsx/.pptx document. Supported paired operations are "
                "append_word_text/remove_word_text, set_excel_cell/clear_excel_cell, and append_powerpoint_slide/remove_powerpoint_slide; "
                "replace_word_text is also supported. Rose edits a private copy and replaces the original only after success.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "path", .description = "Absolute path of the existing Office document.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "operation", .description = "Exact supported Office mutation operation.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "text", .description = "Text/value for append/remove/set/append-slide operations. Encode newlines as literal \\n.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "find", .description = "Exact Word text to replace.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "replacement", .description = "Replacement Word text.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "sheet", .description = "Excel worksheet name.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "cell", .description = "Uppercase A1-style Excel cell reference, such as B7.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "slide_index", .description = "1-based PowerPoint slide index for removal.", .type = ToolValueType::Integer, .required = false }
            }
        }
    {
    }

    const ToolDescriptor& EditOfficeDocumentTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult EditOfficeDocumentTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{ "EditOfficeDocumentTool received a request for a different tool." };
        }
        rejectUnknownArguments(request);

        const std::filesystem::path path{ requiredArgument(request, "path") };
        if (!path.is_absolute())
        {
            throw std::invalid_argument{ "edit_office_document requires an absolute file path." };
        }

        const auto result = mutationService_.edit(documents::EditOfficeDocumentRequest{
            .path = path,
            .kind = parseOperation(requiredArgument(request, "operation")),
            .text = decodeTextEscapes(optionalArgument(request, "text")),
            .findText = decodeTextEscapes(optionalArgument(request, "find")),
            .replacementText = decodeTextEscapes(optionalArgument(request, "replacement")),
            .sheetName = decodeTextEscapes(optionalArgument(request, "sheet")),
            .cellReference = optionalArgument(request, "cell"),
            .slideIndex = parseSlideIndex(optionalArgument(request, "slide_index"))
        });

        return ToolResult{
            .success = true,
            .message =
                "Edited Office document:\npath=" + path.lexically_normal().string()
                + "\noperation=" + result.operation
                + "\naffected_count=" + std::to_string(result.affectedCount)
                + "\nProject Knowledge may need /knowledge index to refresh persisted document excerpts.",
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
