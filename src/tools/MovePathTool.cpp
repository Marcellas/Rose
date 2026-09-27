#include "tools/MovePathTool.h"

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

                if (
                    name != "source"
                    && name != "destination")
                {
                    throw std::invalid_argument{
                        "Tool 'move_path' does not accept argument '"
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


    MovePathTool::MovePathTool()
        : descriptor_{
            .id = "move_path",
            .displayName = "Move or Rename Path",
            .description =
                "Move or rename one existing file or directory from one explicit "
                "absolute path to another. The destination must not already exist, "
                "so Rose never overwrites data during an organizing move.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "source",
                    .description =
                        "Absolute path of the existing file or directory to move.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "destination",
                    .description =
                        "Absolute destination path. It must not already exist.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
    }


    const ToolDescriptor& MovePathTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult MovePathTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "MovePathTool received a request for a different tool."
            };
        }

        rejectUnknownArguments(request);

        std::filesystem::path source{
            requiredArgument(
                request,
                "source")
        };

        std::filesystem::path destination{
            requiredArgument(
                request,
                "destination")
        };

        if (
            !source.is_absolute()
            || !destination.is_absolute())
        {
            throw std::invalid_argument{
                "move_path requires absolute source and destination paths."
            };
        }

        source = source.lexically_normal();
        destination = destination.lexically_normal();

        if (
            isFilesystemRoot(source)
            || isFilesystemRoot(destination))
        {
            throw std::invalid_argument{
                "move_path will not move or replace a filesystem root."
            };
        }

        if (source == destination)
        {
            throw std::invalid_argument{
                "move_path source and destination are the same path."
            };
        }

        std::error_code error;

        if (!std::filesystem::exists(source, error) || error)
        {
            throw std::runtime_error{
                "move_path source does not exist or cannot be inspected: "
                + source.string()
            };
        }

        error.clear();

        if (std::filesystem::exists(destination, error))
        {
            throw std::runtime_error{
                "move_path will not overwrite an existing destination: "
                + destination.string()
            };
        }

        if (error)
        {
            throw std::system_error{
                error,
                "Could not inspect move_path destination"
            };
        }

        const std::filesystem::path destinationParent =
            destination.parent_path();

        error.clear();

        if (
            destinationParent.empty()
            || !std::filesystem::exists(destinationParent, error)
            || error
            || !std::filesystem::is_directory(destinationParent, error)
            || error)
        {
            throw std::runtime_error{
                "move_path requires an existing destination parent directory: "
                + destinationParent.string()
            };
        }

        // Prevent the obvious directory-into-itself case before relying on the OS.
        error.clear();

        if (std::filesystem::is_directory(source, error) && !error)
        {
            const std::filesystem::path canonicalSource =
                std::filesystem::weakly_canonical(source, error);

            if (!error)
            {
                error.clear();

                const std::filesystem::path canonicalParent =
                    std::filesystem::weakly_canonical(
                        destinationParent,
                        error);

                if (!error)
                {
                    const auto sourceIt = canonicalSource.begin();
                    (void)sourceIt;

                    auto sourcePart = canonicalSource.begin();
                    auto parentPart = canonicalParent.begin();

                    bool sourceIsPrefix = true;

                    for (
                        ; sourcePart != canonicalSource.end();
                        ++sourcePart, ++parentPart)
                    {
                        if (
                            parentPart == canonicalParent.end()
                            || *sourcePart != *parentPart)
                        {
                            sourceIsPrefix = false;
                            break;
                        }
                    }

                    if (sourceIsPrefix)
                    {
                        throw std::invalid_argument{
                            "move_path will not move a directory inside itself."
                        };
                    }
                }
            }
        }

        error.clear();
        std::filesystem::rename(
            source,
            destination,
            error);

        if (error)
        {
            throw std::system_error{
                error,
                "Could not move path. Cross-volume moves may require a later copy-and-verify workflow"
            };
        }

        return ToolResult{
            .success = true,
            .message =
                "Moved path:\nfrom="
                + source.string()
                + "\nto="
                + destination.string(),
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }

} // namespace rose::tools
