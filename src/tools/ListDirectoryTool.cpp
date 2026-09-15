#include "tools/ListDirectoryTool.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rose::tools
{
    namespace
    {
        struct DirectoryEntrySummary
        {
            std::string name;
            std::string type;
            std::uintmax_t size{ 0 };
            bool hasSize{ false };
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

                if (name != "path")
                {
                    throw std::invalid_argument{
                        "Tool 'list_directory' does not accept argument '"
                        + name
                        + "'."
                    };
                }
            }
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
        DirectoryEntrySummary summarizeEntry(
            const std::filesystem::directory_entry& entry)
        {
            DirectoryEntrySummary summary;
            summary.name =
                sanitizeSingleLine(
                    entry.path().filename().string());

            std::error_code error;
            const std::filesystem::file_status status =
                entry.symlink_status(error);

            if (error)
            {
                summary.type = "unknown";
                return summary;
            }

            if (std::filesystem::is_symlink(status))
            {
                // Deliberately do not call status() here. status() follows the
                // link; symlink_status() lets Rose describe it without traversing.
                summary.type = "symlink";
                return summary;
            }

            if (std::filesystem::is_directory(status))
            {
                summary.type = "directory";
                return summary;
            }

            if (std::filesystem::is_regular_file(status))
            {
                summary.type = "file";

                const std::uintmax_t size =
                    entry.file_size(error);

                if (!error)
                {
                    summary.size = size;
                    summary.hasSize = true;
                }

                return summary;
            }

            summary.type = "other";
            return summary;
        }


        [[nodiscard]]
        std::string buildEntryLine(
            const DirectoryEntrySummary& entry)
        {
            std::ostringstream line;

            if (entry.type == "directory")
            {
                line << "[DIR]  ";
            }
            else if (entry.type == "file")
            {
                line << "[FILE] ";
            }
            else if (entry.type == "symlink")
            {
                line << "[LINK] ";
            }
            else
            {
                line << "[OTHER] ";
            }

            line << entry.name;

            if (entry.hasSize)
            {
                line
                    << " | bytes="
                    << entry.size;
            }

            line << '\n';
            return line.str();
        }
    } // namespace


    ListDirectoryTool::ListDirectoryTool(
        const ListDirectoryToolConfig config)
        : config_{ config }
        , descriptor_{
            .id = "list_directory",
            .displayName = "List Directory",
            .description =
                "List one level of an existing absolute directory path. "
                "The listing is read-only, non-recursive, bounded, and does not "
                "follow symbolic links. Use it to discover exact file names before "
                "requesting a file read.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description =
                        "Absolute path of the directory to list.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
        if (config_.maximumEntries == 0)
        {
            throw std::invalid_argument{
                "ListDirectoryTool maximumEntries must be greater than zero."
            };
        }

        if (config_.maximumOutputBytes == 0)
        {
            throw std::invalid_argument{
                "ListDirectoryTool maximumOutputBytes must be greater than zero."
            };
        }
    }


    const ToolDescriptor& ListDirectoryTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult ListDirectoryTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "ListDirectoryTool received a request for a different tool."
            };
        }

        rejectUnknownArguments(request);

        const std::filesystem::path path{
            requiredArgument(
                request,
                "path")
        };

        if (!path.is_absolute())
        {
            throw std::invalid_argument{
                "list_directory requires an absolute directory path."
            };
        }

        std::error_code error;

        if (!std::filesystem::exists(path, error) || error)
        {
            throw std::runtime_error{
                "Directory does not exist or cannot be inspected: "
                + path.string()
            };
        }

        if (!std::filesystem::is_directory(path, error) || error)
        {
            throw std::runtime_error{
                "Path is not a directory: "
                + path.string()
            };
        }

        std::vector<DirectoryEntrySummary> entries;
        entries.reserve(
            (std::min)(
                config_.maximumEntries,
                static_cast<std::size_t>(256)));

        bool truncatedByEntryCount{ false };

        std::filesystem::directory_iterator iterator{
            path,
            std::filesystem::directory_options::skip_permission_denied,
            error
        };

        if (error)
        {
            throw std::runtime_error{
                "Could not enumerate directory: "
                + path.string()
            };
        }

        const std::filesystem::directory_iterator end;

        for (; iterator != end; iterator.increment(error))
        {
            if (error)
            {
                // A single inaccessible child should not turn a successfully
                // opened directory into an unbounded retry loop. Stop and report
                // the listing as partial.
                truncatedByEntryCount = true;
                break;
            }

            if (entries.size() >= config_.maximumEntries)
            {
                truncatedByEntryCount = true;
                break;
            }

            entries.push_back(
                summarizeEntry(*iterator));
        }

        std::sort(
            entries.begin(),
            entries.end(),
            [](const DirectoryEntrySummary& left,
               const DirectoryEntrySummary& right)
            {
                return left.name < right.name;
            });

        std::ostringstream header;
        header
            << "Listed directory: "
            << path.string()
            << "\n"
            << "entries_returned="
            << entries.size()
            << "\n"
            << "entry_limit="
            << config_.maximumEntries
            << "\n"
            << "<rose_untrusted_directory_listing>\n";

        std::string output =
            header.str();

        bool truncatedByBytes{ false };

        for (const DirectoryEntrySummary& entry : entries)
        {
            const std::string line =
                buildEntryLine(entry);

            constexpr std::string_view closing{
                "</rose_untrusted_directory_listing>\n"
            };

            if (
                output.size()
                + line.size()
                + closing.size()
                > config_.maximumOutputBytes)
            {
                truncatedByBytes = true;
                break;
            }

            output += line;
        }

        output +=
            "</rose_untrusted_directory_listing>\n";

        const bool truncated =
            truncatedByEntryCount
            || truncatedByBytes;

        output +=
            "listing_truncated="
            + std::string{
                truncated ? "true" : "false"
            };

        if (truncated)
        {
            output +=
                "\nThe directory listing was intentionally bounded. "
                "No recursive traversal occurred.";
        }

        return ToolResult{
            .success = true,
            .message = std::move(output),
            .artifacts = {}
        };
    }

} // namespace rose::tools
