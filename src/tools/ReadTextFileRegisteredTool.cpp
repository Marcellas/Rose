#include "tools/ReadTextFileRegisteredTool.h"

#include "permissions/PermissionSystem.h"
#include "tools/ReadFileTool.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace rose::tools
{
    namespace
    {
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
                        "Tool 'read_text_file' does not accept argument '"
                        + name
                        + "'."
                    };
                }
            }
        }


        // Return the largest UTF-8 prefix not exceeding maximumBytes.
        // ReadFileTool has already validated the complete returned string as UTF-8;
        // we only need to avoid cutting through a multibyte code point.
        [[nodiscard]]
        std::string boundedUtf8Prefix(
            const std::string& text,
            const std::size_t maximumBytes)
        {
            if (text.size() <= maximumBytes)
            {
                return text;
            }

            std::size_t end = maximumBytes;

            while (
                end > 0
                && end < text.size()
                && (
                    static_cast<unsigned char>(
                        text[end])
                    & 0xC0u)
                    == 0x80u)
            {
                --end;
            }

            return text.substr(
                0,
                end);
        }
    } // namespace


    ReadTextFileRegisteredTool::ReadTextFileRegisteredTool(
        permissions::PermissionSystem& permissions,
        ReadFileTool& readFileTool,
        const ReadTextFileRegisteredToolConfig config)
        : permissions_{ permissions }
        , readFileTool_{ readFileTool }
        , config_{ config }
        , descriptor_{
            .id = "read_text_file",
            .displayName = "Read Text File",
            .description =
                "Read the beginning of one exact existing UTF-8 text or source "
                "file at an absolute path. The read is exact-file only and bounded "
                "for model context. It does not read directories, PDFs, images, or "
                "other binary files.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description =
                        "Absolute path of the exact UTF-8 text/source file to read.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
        if (config_.maximumObservationBytes == 0)
        {
            throw std::invalid_argument{
                "ReadTextFileRegisteredTool maximumObservationBytes must be greater than zero."
            };
        }
    }


    const ToolDescriptor& ReadTextFileRegisteredTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult ReadTextFileRegisteredTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "ReadTextFileRegisteredTool received a request for a different tool."
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
                "read_text_file requires an absolute file path."
            };
        }

        // ToolExecutionPolicy has already authorized this exact ToolRequest before
        // ToolRegistry reaches execute(). Translate that authorization into the
        // lower-level exact one-shot grant required by the proven ReadFileTool.
        permissions_.grantReadOnce(path);

        ReadTextFileResult read =
            readFileTool_.readTextFile(path);

        std::string returnedText =
            boundedUtf8Prefix(
                read.text,
                config_.maximumObservationBytes);

        const bool observationTruncated =
            read.truncated
            || returnedText.size() < read.text.size();

        std::ostringstream message;
        message
            << "Read text file: "
            << read.path.string()
            << "\n"
            << "original_size_bytes="
            << read.originalSize
            << "\n"
            << "returned_content_bytes="
            << returnedText.size()
            << "\n"
            << "content_truncated="
            << (observationTruncated ? "true" : "false")
            << "\n"
            << "<rose_untrusted_file_content>\n"
            << returnedText
            << "\n</rose_untrusted_file_content>";

        if (observationTruncated)
        {
            message
                << "\nOnly the beginning of the file was returned because Rose's "
                   "current Agent read observation is deliberately bounded.";
        }

        return ToolResult{
            .success = true,
            .message = message.str(),
            .artifacts = {}
        };
    }

} // namespace rose::tools
