#include "tools/CreateDirectoryTool.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

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

            if (
                found == request.arguments.end()
                || found->second.empty())
            {
                throw std::invalid_argument{
                    "Tool '"
                    + request.toolId
                    + "' requires argument '"
                    + std::string{ name }
                    + "'."
                };
            }

            return found->second;
        }


        void rejectUnknownArguments(
            const ToolRequest& request)
        {
            for (const auto& [name, value] : request.arguments)
            {
                (void)value;

                if (name != "path")
                {
                    throw std::invalid_argument{
                        "Tool 'create_directory' does not accept argument '"
                        + name
                        + "'."
                    };
                }
            }
        }


        [[nodiscard]]
        bool isFilesystemRoot(
            const std::filesystem::path& path)
        {
            const std::filesystem::path normalized =
                path.lexically_normal();

            return !normalized.root_path().empty()
                && normalized == normalized.root_path();
        }
    } // namespace


    CreateDirectoryTool::CreateDirectoryTool()
        : descriptor_{
            .id = "create_directory",
            .displayName = "Create Directory",
            .description =
                "Create exactly one new directory at an explicit absolute path. "
                "The parent directory must already exist. Existing files or "
                "directories are never replaced.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description =
                        "Absolute path of the new directory to create.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
    }


    const ToolDescriptor& CreateDirectoryTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult CreateDirectoryTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "CreateDirectoryTool received a request for a different tool."
            };
        }

        rejectUnknownArguments(request);

        std::filesystem::path path{
            requiredArgument(
                request,
                "path")
        };

        if (!path.is_absolute())
        {
            throw std::invalid_argument{
                "create_directory requires an absolute path."
            };
        }

        path = path.lexically_normal();

        if (
            path.empty()
            || isFilesystemRoot(path))
        {
            throw std::invalid_argument{
                "create_directory will not operate on a filesystem root."
            };
        }

        const std::filesystem::path parent =
            path.parent_path();

        std::error_code error;

        if (
            parent.empty()
            || !std::filesystem::exists(parent, error)
            || error
            || !std::filesystem::is_directory(parent, error)
            || error)
        {
            throw std::runtime_error{
                "create_directory requires an existing parent directory: "
                + parent.string()
            };
        }

        error.clear();

        if (std::filesystem::exists(path, error))
        {
            throw std::runtime_error{
                "create_directory will not reuse or replace an existing path: "
                + path.string()
            };
        }

        if (error)
        {
            throw std::system_error{
                error,
                "Could not inspect create_directory destination"
            };
        }

        error.clear();

        if (!std::filesystem::create_directory(path, error))
        {
            if (error)
            {
                throw std::system_error{
                    error,
                    "Could not create directory"
                };
            }

            throw std::runtime_error{
                "create_directory did not create the requested path: "
                + path.string()
            };
        }

        return ToolResult{
            .success = true,
            .message =
                "Created directory: "
                + path.string(),
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }

} // namespace rose::tools
