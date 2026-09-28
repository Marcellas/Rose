#include "tools/ScanDirectoryTreeTool.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace rose::tools
{
    namespace
    {
        struct EntrySummary
        {
            std::string relativePath;
            std::string absolutePath;
            std::string type;
            std::uintmax_t size{ 0 };
            bool hasSize{ false };
        };


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
                    name != "path"
                    && name != "max_depth")
                {
                    throw std::invalid_argument{
                        "Tool 'scan_directory_tree' does not accept argument '"
                        + name
                        + "'."
                    };
                }
            }
        }


        [[nodiscard]]
        std::size_t parseDepth(
            const ToolRequest& request,
            const std::size_t configuredMaximum)
        {
            const auto found = request.arguments.find("max_depth");

            if (found == request.arguments.end())
            {
                return configuredMaximum;
            }

            std::size_t consumed{ 0 };
            unsigned long long value{ 0 };

            try
            {
                value = std::stoull(found->second, &consumed, 10);
            }
            catch (const std::exception&)
            {
                throw std::invalid_argument{
                    "scan_directory_tree max_depth must be a positive integer."
                };
            }

            if (
                consumed != found->second.size()
                || value == 0
                || value > configuredMaximum)
            {
                throw std::invalid_argument{
                    "scan_directory_tree max_depth must be within 1.."
                    + std::to_string(configuredMaximum)
                    + "."
                };
            }

            return static_cast<std::size_t>(value);
        }


        [[nodiscard]]
        std::string sanitizeSingleLine(
            std::string value)
        {
            for (char& character : value)
            {
                if (
                    character == '\r'
                    || character == '\n'
                    || character == '\t')
                {
                    character = ' ';
                }
            }

            return value;
        }


        [[nodiscard]]
        std::string lowerAscii(
            std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(
                        std::tolower(character));
                });

            return value;
        }


        [[nodiscard]]
        std::size_t relativePathDepth(
            const std::string_view path) noexcept
        {
            return static_cast<std::size_t>(
                std::count_if(
                    path.begin(),
                    path.end(),
                    [](const char character)
                    {
                        return character == '\\' || character == '/';
                    }));
        }


        [[nodiscard]]
        std::string_view firstPathComponent(
            const std::string_view path) noexcept
        {
            const std::size_t separator =
                path.find_first_of("\\/");

            return path.substr(0, separator);
        }


        [[nodiscard]]
        bool conventionalGeneratedTree(
            const std::string_view relativePath)
        {
            const std::string first =
                lowerAscii(
                    std::string{
                        firstPathComponent(relativePath)
                    });

            return
                first == ".git"
                || first == ".vs"
                || first == ".cache"
                || first == "build"
                || first.starts_with("build-")
                || first == "out"
                || first == "dist"
                || first == "target"
                || first == "external"
                || first == "vendor"
                || first == "node_modules"
                || first == "packages"
                || first == "models";
        }
    } // namespace


    ScanDirectoryTreeTool::ScanDirectoryTreeTool(
        const ScanDirectoryTreeToolConfig config)
        : config_{ config }
        , descriptor_{
            .id = "scan_directory_tree",
            .displayName = "Scan Directory Tree",
            .description =
                "Recursively inventory an existing absolute directory without "
                "opening file contents. The scan is bounded, does not follow "
                "symbolic links, and reports relative paths, Rose-grounded absolute "
                "paths, types, file sizes, and extension counts. Use it to plan "
                "large-batch or whole-directory analysis and targeted follow-up.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description =
                        "Absolute root directory to inventory recursively.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "max_depth",
                    .description =
                        "Optional recursion depth. Omit to use Rose's configured safe maximum.",
                    .type = ToolValueType::Integer,
                    .required = false
                }
            }
        }
    {
        if (
            config_.maximumEntries == 0
            || config_.maximumOutputBytes == 0
            || config_.maximumDepth == 0)
        {
            throw std::invalid_argument{
                "ScanDirectoryTreeTool limits must all be greater than zero."
            };
        }
    }


    const ToolDescriptor& ScanDirectoryTreeTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult ScanDirectoryTreeTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "ScanDirectoryTreeTool received a request for a different tool."
            };
        }

        rejectUnknownArguments(request);

        std::filesystem::path root{
            requiredArgument(
                request,
                "path")
        };

        if (!root.is_absolute())
        {
            throw std::invalid_argument{
                "scan_directory_tree requires an absolute directory path."
            };
        }

        root = root.lexically_normal();

        const std::size_t requestedDepth =
            parseDepth(
                request,
                config_.maximumDepth);

        std::error_code error;

        if (
            !std::filesystem::exists(root, error)
            || error
            || !std::filesystem::is_directory(root, error)
            || error)
        {
            throw std::runtime_error{
                "scan_directory_tree root does not exist or is not a readable directory: "
                + root.string()
            };
        }

        std::vector<EntrySummary> entries;
        entries.reserve(
            (std::min)(
                config_.maximumEntries,
                static_cast<std::size_t>(2048)));

        std::map<std::string, std::size_t> extensionCounts;
        std::size_t directoryCount{ 0 };
        std::size_t fileCount{ 0 };
        std::size_t symlinkCount{ 0 };
        std::uintmax_t totalKnownFileBytes{ 0 };
        bool truncated{ false };
        bool partialBecauseOfError{ false };

        std::filesystem::recursive_directory_iterator iterator{
            root,
            std::filesystem::directory_options::skip_permission_denied,
            error
        };

        if (error)
        {
            throw std::runtime_error{
                "Could not begin recursive directory scan: "
                + root.string()
            };
        }

        const std::filesystem::recursive_directory_iterator end;

        for (; iterator != end; iterator.increment(error))
        {
            if (error)
            {
                partialBecauseOfError = true;
                error.clear();
                continue;
            }

            if (entries.size() >= config_.maximumEntries)
            {
                truncated = true;
                break;
            }

            if (
                static_cast<std::size_t>(iterator.depth()) + 1u
                >= requestedDepth)
            {
                iterator.disable_recursion_pending();
            }

            const std::filesystem::directory_entry& entry =
                *iterator;

            EntrySummary summary;
            summary.absolutePath =
                sanitizeSingleLine(
                    entry.path().lexically_normal().string());

            std::error_code relativeError;
            summary.relativePath =
                sanitizeSingleLine(
                    std::filesystem::relative(
                        entry.path(),
                        root,
                        relativeError).string());

            if (relativeError)
            {
                summary.relativePath =
                    sanitizeSingleLine(
                        entry.path().filename().string());
            }

            std::error_code statusError;
            const std::filesystem::file_status status =
                entry.symlink_status(statusError);

            if (statusError)
            {
                summary.type = "unknown";
                partialBecauseOfError = true;
            }
            else if (std::filesystem::is_symlink(status))
            {
                summary.type = "symlink";
                ++symlinkCount;
                iterator.disable_recursion_pending();
            }
            else if (std::filesystem::is_directory(status))
            {
                summary.type = "directory";
                ++directoryCount;
            }
            else if (std::filesystem::is_regular_file(status))
            {
                summary.type = "file";
                ++fileCount;

                std::error_code sizeError;
                const std::uintmax_t size =
                    entry.file_size(sizeError);

                if (!sizeError)
                {
                    summary.size = size;
                    summary.hasSize = true;
                    totalKnownFileBytes += size;
                }

                std::string extension =
                    lowerAscii(
                        entry.path().extension().string());

                if (extension.empty())
                {
                    extension = "<none>";
                }

                ++extensionCounts[extension];
            }
            else
            {
                summary.type = "other";
            }

            entries.push_back(
                std::move(summary));
        }

        std::sort(
            entries.begin(),
            entries.end(),
            [](const EntrySummary& left,
               const EntrySummary& right)
            {
                const std::size_t leftDepth =
                    relativePathDepth(left.relativePath);
                const std::size_t rightDepth =
                    relativePathDepth(right.relativePath);

                if (leftDepth != rightDepth)
                {
                    return leftDepth < rightDepth;
                }

                // The observation budget is intentionally much smaller than the
                // scan budget. Put ordinary/project-owned paths before conventional
                // generated trees so a bounded observation exposes useful grounded
                // follow-up candidates instead of spending all of its bytes on
                // .git/build/vendor metadata. Counts above still include everything
                // that the bounded traversal visited.
                const bool leftGenerated =
                    conventionalGeneratedTree(left.relativePath);
                const bool rightGenerated =
                    conventionalGeneratedTree(right.relativePath);

                if (leftGenerated != rightGenerated)
                {
                    return !leftGenerated;
                }

                return left.relativePath < right.relativePath;
            });

        std::ostringstream message;
        message
            << "Scanned directory tree: "
            << root.string()
            << "\n"
            << "max_depth="
            << requestedDepth
            << "\n"
            << "entries_returned="
            << entries.size()
            << "\n"
            << "directories_seen="
            << directoryCount
            << "\n"
            << "files_seen="
            << fileCount
            << "\n"
            << "symlinks_seen="
            << symlinkCount
            << "\n"
            << "known_file_bytes="
            << totalKnownFileBytes
            << "\n"
            << "truncated="
            << (truncated ? "true" : "false")
            << "\n"
            << "partial_due_to_access_or_metadata_error="
            << (partialBecauseOfError ? "true" : "false")
            << "\nextensions:";

        for (const auto& [extension, count] : extensionCounts)
        {
            message
                << "\n  "
                << extension
                << "="
                << count;
        }

        message << "\nentries:";

        bool outputTruncated{ false };

        for (const EntrySummary& entry : entries)
        {
            std::ostringstream line;
            line
                << "\n["
                << entry.type
                << "] "
                << entry.relativePath
                << " | absolute_path="
                << entry.absolutePath;

            if (entry.hasSize)
            {
                line
                    << " | bytes="
                    << entry.size;
            }

            const std::string lineText =
                line.str();

            if (
                message.tellp() >= 0
                && static_cast<std::size_t>(message.tellp())
                    + lineText.size()
                    > config_.maximumOutputBytes)
            {
                outputTruncated = true;
                break;
            }

            message << lineText;
        }

        if (outputTruncated)
        {
            message
                << "\n[Rose truncated the entry listing to keep the observation bounded.]";
        }

        return ToolResult{
            .success = true,
            .message = message.str(),
            .responseMode = ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };
    }

} // namespace rose::tools
