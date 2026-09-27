#include "tools/CreateOfficeDocumentTool.h"

#include <algorithm>
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
        documents::OfficeDocumentKind parseKind(const std::string_view raw)
        {
            const std::string value = lowerAscii(std::string{ raw });
            if (value == "word" || value == "docx") return documents::OfficeDocumentKind::Word;
            if (value == "excel" || value == "xlsx" || value == "spreadsheet") return documents::OfficeDocumentKind::Excel;
            if (value == "powerpoint" || value == "pptx" || value == "presentation") return documents::OfficeDocumentKind::PowerPoint;
            throw std::invalid_argument{
                "create_office_document kind must be word, excel, or powerpoint."
            };
        }

        void rejectUnknownArguments(const ToolRequest& request)
        {
            for (const auto& [name, value] : request.arguments)
            {
                (void)value;
                if (name != "path" && name != "kind" && name != "content" && name != "sheet")
                {
                    throw std::invalid_argument{
                        "Tool 'create_office_document' does not accept argument '" + name + "'."
                    };
                }
            }
        }
    }

    CreateOfficeDocumentTool::CreateOfficeDocumentTool(
        documents::IOfficeDocumentMutationService& mutationService)
        : mutationService_{ mutationService }
        , descriptor_{
            .id = "create_office_document",
            .displayName = "Create Office Document",
            .description =
                "Create one NEW .docx, .xlsx, or .pptx document at an explicit absolute path. "
                "Rose never overwrites an existing file. Word/Excel creation is direct Open XML; "
                "PowerPoint creation uses the locally installed PowerPoint application in this checkpoint.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "path", .description = "Absolute path of the NEW .docx/.xlsx/.pptx file.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "kind", .description = "word, excel, or powerpoint.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "content", .description = "Optional initial text. Encode line breaks as literal \\n sequences.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "sheet", .description = "Optional initial Excel worksheet name. Defaults to Sheet1.", .type = ToolValueType::String, .required = false }
            }
        }
    {
    }

    const ToolDescriptor& CreateOfficeDocumentTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult CreateOfficeDocumentTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{ "CreateOfficeDocumentTool received a request for a different tool." };
        }
        rejectUnknownArguments(request);

        const std::filesystem::path path{ requiredArgument(request, "path") };
        if (!path.is_absolute())
        {
            throw std::invalid_argument{ "create_office_document requires an absolute destination path." };
        }

        const auto result = mutationService_.create(documents::CreateOfficeDocumentRequest{
            .path = path,
            .kind = parseKind(requiredArgument(request, "kind")),
            .content = decodeTextEscapes(optionalArgument(request, "content")),
            .sheetName = [&]
            {
                std::string sheet = decodeTextEscapes(optionalArgument(request, "sheet"));
                return sheet.empty() ? std::string{ "Sheet1" } : sheet;
            }()
        });

        return ToolResult{
            .success = true,
            .message =
                "Created Office document:\npath=" + path.lexically_normal().string()
                + "\noperation=" + result.operation
                + "\nThe inverse is recycle_path if you want Rose to remove the newly created document.",
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
