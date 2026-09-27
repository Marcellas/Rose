#include "tools/BatchMovePathsTool.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rose::tools
{
    namespace
    {
        struct MoveOperation
        {
            std::filesystem::path source;
            std::filesystem::path destination;
        };


        [[nodiscard]]
        const std::string& requiredArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

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

                if (name != "operations")
                {
                    throw std::invalid_argument{
                        "Tool 'batch_move_paths' does not accept argument '"
                        + name
                        + "'."
                    };
                }
            }
        }


        [[nodiscard]]
        std::string trimCopy(
            const std::string_view text)
        {
            std::size_t begin{ 0 };
            std::size_t end = text.size();

            while (
                begin < end
                && std::isspace(
                    static_cast<unsigned char>(
                        text[begin])) != 0)
            {
                ++begin;
            }

            while (
                end > begin
                && std::isspace(
                    static_cast<unsigned char>(
                        text[end - 1])) != 0)
            {
                --end;
            }

            return std::string{
                text.substr(
                    begin,
                    end - begin)
            };
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


        [[nodiscard]]
        std::string pathKey(
            const std::filesystem::path& path)
        {
            std::string key =
                path.lexically_normal().generic_string();

#ifdef _WIN32
            std::transform(
                key.begin(),
                key.end(),
                key.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(
                        std::tolower(character));
                });
#endif

            return key;
        }


        [[nodiscard]]
        std::vector<MoveOperation> parseOperations(
            const std::string_view encoded,
            const std::size_t maximumOperations)
        {
            // Format is deliberately simple and Windows-safe:
            //
            //   source=>destination|source=>destination
            //
            // '|' and '>' are illegal in Windows filenames, so no escaping is
            // needed for the platform Rose currently targets.
            std::vector<MoveOperation> operations;

            std::size_t begin{ 0 };

            while (begin < encoded.size())
            {
                const std::size_t separator =
                    encoded.find('|', begin);

                const std::string_view item =
                    encoded.substr(
                        begin,
                        separator == std::string_view::npos
                            ? std::string_view::npos
                            : separator - begin);

                const std::size_t arrow =
                    item.find("=>");

                if (
                    arrow == std::string_view::npos
                    || item.find("=>", arrow + 2)
                        != std::string_view::npos)
                {
                    throw std::invalid_argument{
                        "batch_move_paths operations must use "
                        "source=>destination pairs separated by '|'."
                    };
                }

                const std::string source =
                    trimCopy(
                        item.substr(0, arrow));

                const std::string destination =
                    trimCopy(
                        item.substr(arrow + 2));

                if (
                    source.empty()
                    || destination.empty())
                {
                    throw std::invalid_argument{
                        "batch_move_paths contains an empty source or destination."
                    };
                }

                operations.push_back(
                    MoveOperation{
                        .source = std::filesystem::path{ source }.lexically_normal(),
                        .destination = std::filesystem::path{ destination }.lexically_normal()
                    });

                if (operations.size() > maximumOperations)
                {
                    throw std::invalid_argument{
                        "batch_move_paths exceeds Rose's configured operation limit of "
                        + std::to_string(maximumOperations)
                        + "."
                    };
                }

                if (separator == std::string_view::npos)
                {
                    break;
                }

                begin = separator + 1;
            }

            if (operations.empty())
            {
                throw std::invalid_argument{
                    "batch_move_paths requires at least one move operation."
                };
            }

            return operations;
        }


        void preflight(
            const std::vector<MoveOperation>& operations)
        {
            std::unordered_set<std::string> sources;
            std::unordered_set<std::string> destinations;

            for (const MoveOperation& operation : operations)
            {
                if (
                    !operation.source.is_absolute()
                    || !operation.destination.is_absolute())
                {
                    throw std::invalid_argument{
                        "batch_move_paths requires absolute source and destination paths."
                    };
                }

                if (
                    isFilesystemRoot(operation.source)
                    || isFilesystemRoot(operation.destination))
                {
                    throw std::invalid_argument{
                        "batch_move_paths will not move or replace a filesystem root."
                    };
                }

                if (operation.source == operation.destination)
                {
                    throw std::invalid_argument{
                        "batch_move_paths contains an operation whose source and destination are identical."
                    };
                }

                const std::string sourceKey =
                    pathKey(operation.source);

                const std::string destinationKey =
                    pathKey(operation.destination);

                if (!sources.insert(sourceKey).second)
                {
                    throw std::invalid_argument{
                        "batch_move_paths contains a duplicate source path: "
                        + operation.source.string()
                    };
                }

                if (!destinations.insert(destinationKey).second)
                {
                    throw std::invalid_argument{
                        "batch_move_paths contains a duplicate destination path: "
                        + operation.destination.string()
                    };
                }

                std::error_code error;

                if (
                    !std::filesystem::exists(operation.source, error)
                    || error)
                {
                    throw std::runtime_error{
                        "batch_move_paths source does not exist or cannot be inspected: "
                        + operation.source.string()
                    };
                }

                error.clear();

                if (
                    std::filesystem::is_regular_file(operation.source, error)
                    && !error)
                {
                    std::string sourceExtension =
                        operation.source.extension().string();

                    std::string destinationExtension =
                        operation.destination.extension().string();

#ifdef _WIN32
                    std::transform(
                        sourceExtension.begin(),
                        sourceExtension.end(),
                        sourceExtension.begin(),
                        [](const unsigned char character)
                        {
                            return static_cast<char>(std::tolower(character));
                        });

                    std::transform(
                        destinationExtension.begin(),
                        destinationExtension.end(),
                        destinationExtension.begin(),
                        [](const unsigned char character)
                        {
                            return static_cast<char>(std::tolower(character));
                        });
#endif

                    if (sourceExtension != destinationExtension)
                    {
                        throw std::invalid_argument{
                            "batch_move_paths preserves file extensions during bulk "
                            "organization. Source and destination extensions differ: "
                            + operation.source.string()
                            + " -> "
                            + operation.destination.string()
                        };
                    }
                }

                error.clear();

                if (std::filesystem::exists(operation.destination, error))
                {
                    throw std::runtime_error{
                        "batch_move_paths will not overwrite existing destination: "
                        + operation.destination.string()
                    };
                }

                if (error)
                {
                    throw std::system_error{
                        error,
                        "Could not inspect batch_move_paths destination"
                    };
                }

                const std::filesystem::path parent =
                    operation.destination.parent_path();

                error.clear();

                if (
                    parent.empty()
                    || !std::filesystem::exists(parent, error)
                    || error
                    || !std::filesystem::is_directory(parent, error)
                    || error)
                {
                    throw std::runtime_error{
                        "batch_move_paths requires an existing destination parent directory: "
                        + parent.string()
                    };
                }
            }

            // Do not allow an operation to target another operation's source. That
            // would make order significant and complicate rollback semantics.
            for (const MoveOperation& operation : operations)
            {
                if (
                    sources.contains(
                        pathKey(operation.destination)))
                {
                    throw std::invalid_argument{
                        "batch_move_paths does not support destination/source chains or swaps in one batch."
                    };
                }
            }
        }
    } // namespace


    BatchMovePathsTool::BatchMovePathsTool(
        const BatchMovePathsToolConfig config)
        : config_{ config }
        , descriptor_{
            .id = "batch_move_paths",
            .displayName = "Batch Move or Rename Paths",
            .description =
                "Move or rename multiple known files/directories as one confirmation-gated "
                "batch. Every destination must be new; Rose preflights the entire plan before "
                "making changes and attempts rollback if a later move fails. Use it after "
                "directory/document analysis when the user asked to organize or rename many files.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "operations",
                    .description =
                        "Single-line batch plan using source=>destination pairs separated by '|'. "
                        "Use exact absolute paths and preserve each file extension. Example: C:\\A.pdf=>C:\\2026-01-01 A.pdf|C:\\B.pdf=>C:\\2026-01-02 B.pdf",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
        if (config_.maximumOperations == 0)
        {
            throw std::invalid_argument{
                "BatchMovePathsTool maximumOperations must be greater than zero."
            };
        }
    }


    const ToolDescriptor& BatchMovePathsTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult BatchMovePathsTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "BatchMovePathsTool received a request for a different tool."
            };
        }

        rejectUnknownArguments(request);

        const std::vector<MoveOperation> operations =
            parseOperations(
                requiredArgument(
                    request,
                    "operations"),
                config_.maximumOperations);

        preflight(operations);

        std::vector<std::size_t> completed;
        completed.reserve(operations.size());

        for (
            std::size_t index{ 0 };
            index < operations.size();
            ++index)
        {
            const MoveOperation& operation =
                operations[index];

            std::error_code error;
            std::filesystem::rename(
                operation.source,
                operation.destination,
                error);

            if (error)
            {
                std::ostringstream rollbackFailures;

                for (auto iterator = completed.rbegin();
                     iterator != completed.rend();
                     ++iterator)
                {
                    const MoveOperation& previous =
                        operations[*iterator];

                    std::error_code rollbackError;
                    std::filesystem::rename(
                        previous.destination,
                        previous.source,
                        rollbackError);

                    if (rollbackError)
                    {
                        rollbackFailures
                            << "\nRollback failed: "
                            << previous.destination.string()
                            << " -> "
                            << previous.source.string()
                            << " ("
                            << rollbackError.message()
                            << ")";
                    }
                }

                throw std::runtime_error{
                    "batch_move_paths failed at operation "
                    + std::to_string(index + 1)
                    + " of "
                    + std::to_string(operations.size())
                    + ": "
                    + operation.source.string()
                    + " -> "
                    + operation.destination.string()
                    + " ("
                    + error.message()
                    + ")"
                    + rollbackFailures.str()
                };
            }

            completed.push_back(index);
        }

        std::ostringstream message;
        message
            << "Completed batch move/rename operation.\n"
            << "operation_count="
            << operations.size();

        for (
            std::size_t index{ 0 };
            index < operations.size();
            ++index)
        {
            message
                << "\n#"
                << (index + 1)
                << " from="
                << operations[index].source.string()
                << "\n#"
                << (index + 1)
                << " to="
                << operations[index].destination.string();
        }

        return ToolResult{
            .success = true,
            .message = message.str(),
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }

} // namespace rose::tools
