#include "tools/ReadTextFileRegisteredTool.h"

#include "permissions/PermissionSystem.h"
#include "files/SourceWindowDigest.h"
#include "tools/ReadFileTool.h"

#include <charconv>
#include <filesystem>
#include <limits>
#include <optional>
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

                if (
                    name != "path"
                    && name != "start_line"
                    && name != "line_count")
                {
                    throw std::invalid_argument{
                        "Tool 'read_text_file' does not accept argument '"
                        + name
                        + "'."
                    };
                }
            }
        }


        [[nodiscard]]
        std::optional<std::size_t> optionalPositiveInteger(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

            if (found == request.arguments.end())
            {
                return std::nullopt;
            }

            if (found->second.empty())
            {
                throw std::invalid_argument{
                    "Tool 'read_text_file' argument '"
                    + std::string{ name }
                    + "' must be a positive integer."
                };
            }

            std::size_t value{ 0 };
            const char* first = found->second.data();
            const char* last = first + found->second.size();
            const auto [end, error] =
                std::from_chars(
                    first,
                    last,
                    value);

            if (
                error != std::errc{}
                || end != last
                || value == 0)
            {
                throw std::invalid_argument{
                    "Tool 'read_text_file' argument '"
                    + std::string{ name }
                    + "' must be a positive integer."
                };
            }

            return value;
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


        [[nodiscard]]
        std::size_t visibleLineCount(
            const std::string_view text) noexcept
        {
            if (text.empty())
            {
                return 0;
            }

            std::size_t lines{ 0 };
            for (const char character : text)
            {
                if (character == '\n')
                {
                    ++lines;
                }
            }

            if (text.back() != '\n')
            {
                ++lines;
            }

            return lines;
        }


        [[nodiscard]]
        std::string numberedSourceWindow(
            const std::string_view text,
            const std::size_t startLine)
        {
            std::ostringstream numbered;
            std::size_t lineNumber = startLine;
            std::size_t begin{ 0 };

            while (begin < text.size())
            {
                const std::size_t newline = text.find('\n', begin);
                const std::size_t end =
                    newline == std::string_view::npos
                        ? text.size()
                        : newline;

                std::size_t contentEnd = end;
                if (contentEnd > begin && text[contentEnd - 1] == '\r')
                {
                    --contentEnd;
                }

                // There is deliberately no formatting space after '|'. Everything
                // after the delimiter is the exact source-line content, including
                // any leading indentation.
                numbered
                    << lineNumber
                    << '|'
                    << text.substr(begin, contentEnd - begin)
                    << '\n';

                ++lineNumber;

                if (newline == std::string_view::npos)
                {
                    break;
                }

                begin = newline + 1;
            }

            return numbered.str();
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
                "Read one exact existing UTF-8 text/source file at an absolute "
                "path. By default Rose returns a bounded prefix. For configure/compiler/test "
                "diagnostics, optional one-based start_line and bounded line_count "
                "return a source window from later in the file without reading a "
                "directory or widening filesystem permission.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description =
                        "Absolute path of the exact UTF-8 text/source file to read.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "start_line",
                    .description =
                        "Optional one-based first line for a bounded source window.",
                    .type = ToolValueType::Integer,
                    .required = false
                },
                ToolParameterDescriptor{
                    .name = "line_count",
                    .description =
                        "Optional number of source lines to return. Requires start_line.",
                    .type = ToolValueType::Integer,
                    .required = false
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

        if (
            config_.defaultLineCount == 0
            || config_.maximumLineCount == 0
            || config_.defaultLineCount > config_.maximumLineCount)
        {
            throw std::invalid_argument{
                "ReadTextFileRegisteredTool line-count bounds are invalid."
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

        const std::optional<std::size_t> startLine =
            optionalPositiveInteger(
                request,
                "start_line");

        const std::optional<std::size_t> lineCount =
            optionalPositiveInteger(
                request,
                "line_count");

        if (!startLine.has_value() && lineCount.has_value())
        {
            throw std::invalid_argument{
                "read_text_file line_count requires start_line."
            };
        }

        if (
            lineCount.has_value()
            && *lineCount > config_.maximumLineCount)
        {
            throw std::invalid_argument{
                "read_text_file line_count exceeds the configured maximum of "
                + std::to_string(config_.maximumLineCount)
                + "."
            };
        }

        // ToolExecutionPolicy has already authorized this exact ToolRequest before
        // ToolRegistry reaches execute(). Translate that authorization into the
        // lower-level exact one-shot grant required by the proven ReadFileTool.
        permissions_.grantReadOnce(path);

        if (startLine.has_value())
        {
            const std::size_t requestedLineCount =
                lineCount.value_or(
                    config_.defaultLineCount);

            ReadTextFileRangeResult read =
                readFileTool_.readTextFileLines(
                    path,
                    *startLine,
                    requestedLineCount);

            std::string returnedText =
                boundedUtf8Prefix(
                    read.text,
                    config_.maximumObservationBytes);

            const bool observationTruncated =
                read.rangeTruncated
                || returnedText.size() < read.text.size();

            const std::size_t shownLines =
                visibleLineCount(returnedText);

            const std::size_t visibleEndLine =
                shownLines == 0
                    ? *startLine
                    : *startLine + shownLines - 1;

            const std::string numberedText =
                numberedSourceWindow(
                    returnedText,
                    *startLine);

            std::ostringstream message;
            message
                << "Read text file source window: "
                << read.path.string()
                << "\n"
                << "original_size_bytes="
                << read.originalSize
                << "\n"
                << "requested_start_line="
                << *startLine
                << "\n"
                << "requested_line_count="
                << requestedLineCount
                << "\n"
                << "returned_end_line="
                << visibleEndLine
                << "\n"
                << "returned_content_bytes="
                << returnedText.size()
                << "\n"
                << "source_scan_truncated="
                << (read.scanTruncated ? "true" : "false")
                << "\n"
                << "content_truncated="
                << (observationTruncated ? "true" : "false")
                << "\n"
                << "numbered_source_format=<one-based-line>|<exact-source-line>\n"
                << "<rose_untrusted_numbered_source>\n"
                << numberedText
                << "</rose_untrusted_numbered_source>";

            if (observationTruncated)
            {
                message
                    << "\nThe requested source window exceeded a bounded Rose read "
                       "limit. Narrow the line window if more exact context is needed.";
            }

            ToolResult result{
                .success = true,
                .message = message.str(),
                .artifacts = {}
            };

            // Only a complete observation can become patch provenance. The digest
            // covers the exact logical preimage (CRLF normalized to LF and without
            // the delimiter after the final selected line), matching the mutation
            // service's replace_line_range comparison semantics.
            if (!observationTruncated && shownLines > 0)
            {
                const std::string logicalWindow =
                    files::canonicalObservedSourceWindow(
                        read.text);

                const std::vector<std::string> lineDigests =
                    files::sourceLineSha256s(
                        logicalWindow);

                const std::string digest =
                    files::sourceWindowSha256FromLineDigests(
                        lineDigests);

                result.sourceWindowEvidence =
                    SourceWindowEvidence{
                        .path = read.path.lexically_normal().string(),
                        .startLine = *startLine,
                        .lineCount = shownLines,
                        .lineSha256s = lineDigests,
                        .sha256 = digest
                    };

                std::ostringstream metadata;
                metadata
                    << "producer_tool=read_text_file\n"
                    << "source_window_path="
                    << read.path.lexically_normal().string()
                    << "\n"
                    << "source_window_start_line="
                    << *startLine
                    << "\n"
                    << "source_window_line_count="
                    << shownLines
                    << "\n"
                    << "source_window_sha256="
                    << digest;

                result.trustedMetadata = metadata.str();
            }

            return result;
        }

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
