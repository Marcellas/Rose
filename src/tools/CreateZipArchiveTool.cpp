#include "tools/CreateZipArchiveTool.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]]
        const std::string& requiredArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            if (found == request.arguments.end() || found->second.empty())
            {
                throw std::invalid_argument{
                    "Tool '" + request.toolId + "' requires argument '"
                    + std::string{ name } + "'."
                };
            }
            return found->second;
        }

        void rejectUnknownArguments(const ToolRequest& request)
        {
            for (const auto& [name, value] : request.arguments)
            {
                (void)value;
                if (name != "source" && name != "destination")
                {
                    throw std::invalid_argument{
                        "Tool 'create_zip_archive' does not accept argument '" + name + "'."
                    };
                }
            }
        }
    }

    CreateZipArchiveTool::CreateZipArchiveTool(
        archives::IZipArchiveService& archiveService)
        : archiveService_{ archiveService }
        , descriptor_{
            .id = "create_zip_archive",
            .displayName = "Create ZIP Archive",
            .description =
                "Create one NEW .zip archive from one exact existing file or "
                "directory. Rose never overwrites an existing archive, skips "
                "symbolic-link sources, and applies bounded entry/size limits.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "source",
                    .description = "Absolute path of the source file or directory.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "destination",
                    .description = "Absolute path of the NEW .zip archive.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
    }

    const ToolDescriptor& CreateZipArchiveTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult CreateZipArchiveTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "CreateZipArchiveTool received a request for a different tool."
            };
        }
        rejectUnknownArguments(request);

        const std::filesystem::path source{ requiredArgument(request, "source") };
        const std::filesystem::path destination{ requiredArgument(request, "destination") };
        if (!source.is_absolute() || !destination.is_absolute())
        {
            throw std::invalid_argument{
                "create_zip_archive requires absolute source and destination paths."
            };
        }

        archiveService_.create(source, destination);
        return ToolResult{
            .success = true,
            .message =
                "Created ZIP archive:\nsource=" + source.lexically_normal().string()
                + "\ndestination=" + destination.lexically_normal().string(),
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
