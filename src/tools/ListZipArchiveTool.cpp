#include "tools/ListZipArchiveTool.h"

#include <filesystem>
#include <sstream>
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
                if (name != "path")
                {
                    throw std::invalid_argument{
                        "Tool 'list_zip_archive' does not accept argument '" + name + "'."
                    };
                }
            }
        }
    }

    ListZipArchiveTool::ListZipArchiveTool(
        archives::IZipArchiveService& archiveService,
        const ListZipArchiveToolConfig config)
        : archiveService_{ archiveService }
        , config_{ config }
        , descriptor_{
            .id = "list_zip_archive",
            .displayName = "Inspect ZIP Archive",
            .description =
                "Inspect one exact .zip archive and return a bounded manifest of "
                "its entries, sizes, and unsafe-path warnings. This is read-only "
                "and does not extract or execute archive contents.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description = "Absolute path of the .zip archive to inspect.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
        if (config_.maximumOutputBytes == 0)
        {
            throw std::invalid_argument{
                "ListZipArchiveTool maximumOutputBytes must be greater than zero."
            };
        }
    }

    const ToolDescriptor& ListZipArchiveTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult ListZipArchiveTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "ListZipArchiveTool received a request for a different tool."
            };
        }
        rejectUnknownArguments(request);

        const std::filesystem::path path{ requiredArgument(request, "path") };
        if (!path.is_absolute())
        {
            throw std::invalid_argument{
                "list_zip_archive requires an absolute .zip path."
            };
        }

        const archives::ZipArchiveListing listing = archiveService_.list(path);

        std::ostringstream output;
        output
            << "ZIP archive: " << path.string() << '\n'
            << "entries=" << listing.totalEntryCount << '\n'
            << "total_uncompressed_bytes=" << listing.totalUncompressedBytes << '\n'
            << "manifest_truncated=" << (listing.truncated ? "true" : "false") << '\n'
            << "contains_unsafe_paths=" << (listing.containsUnsafePaths ? "true" : "false") << '\n'
            << "<rose_untrusted_zip_manifest>\n";

        bool outputTruncated{ false };
        for (const auto& entry : listing.entries)
        {
            std::ostringstream line;
            line
                << (entry.directory ? "[DIR]  " : "[FILE] ")
                << (entry.safeRelativePath ? "" : "[UNSAFE] ")
                << entry.path;
            if (!entry.directory)
            {
                line
                    << " | bytes=" << entry.uncompressedBytes
                    << " | compressed=" << entry.compressedBytes;
            }
            line << '\n';

            const std::string text = line.str();
            if (output.tellp() >= 0
                && static_cast<std::size_t>(output.tellp()) + text.size()
                    > config_.maximumOutputBytes)
            {
                outputTruncated = true;
                break;
            }
            output << text;
        }

        if (outputTruncated)
        {
            output << "[Rose truncated the displayed ZIP manifest.]\n";
        }
        output << "</rose_untrusted_zip_manifest>";

        return ToolResult{
            .success = true,
            .message = output.str(),
            .responseMode = ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };
    }
}
