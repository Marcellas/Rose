#include "tools/ExtractZipArchiveTool.h"

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
                if (name != "path" && name != "destination")
                {
                    throw std::invalid_argument{
                        "Tool 'extract_zip_archive' does not accept argument '" + name + "'."
                    };
                }
            }
        }
    }

    ExtractZipArchiveTool::ExtractZipArchiveTool(
        archives::IZipArchiveService& archiveService)
        : archiveService_{ archiveService }
        , descriptor_{
            .id = "extract_zip_archive",
            .displayName = "Extract ZIP Archive",
            .description =
                "Extract one exact .zip archive into one NEW absolute destination "
                "directory. Rose preflights entry count/size and path traversal, "
                "never overwrites an existing destination, and rolls back a partial "
                "new destination if extraction fails.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description = "Absolute path of the source .zip archive.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "destination",
                    .description = "Absolute path of the NEW destination directory.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
    }

    const ToolDescriptor& ExtractZipArchiveTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult ExtractZipArchiveTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "ExtractZipArchiveTool received a request for a different tool."
            };
        }
        rejectUnknownArguments(request);

        const std::filesystem::path path{ requiredArgument(request, "path") };
        const std::filesystem::path destination{ requiredArgument(request, "destination") };
        if (!path.is_absolute() || !destination.is_absolute())
        {
            throw std::invalid_argument{
                "extract_zip_archive requires absolute source and destination paths."
            };
        }

        archiveService_.extract(path, destination);
        return ToolResult{
            .success = true,
            .message =
                "Extracted ZIP archive:\nsource=" + path.lexically_normal().string()
                + "\ndestination=" + destination.lexically_normal().string(),
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
