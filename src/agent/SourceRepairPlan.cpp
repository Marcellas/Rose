#include "agent/SourceRepairPlan.h"

#include "files/SourceWindowDigest.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace rose::agent
{
    namespace
    {
        [[nodiscard]]
        std::optional<std::string> lineValue(
            const std::string_view metadata,
            const std::string_view key)
        {
            const std::string needle = std::string{ key } + "=";
            std::size_t position = metadata.find(needle);

            while (position != std::string_view::npos)
            {
                if (position == 0 || metadata[position - 1] == '\n')
                {
                    const std::size_t begin = position + needle.size();
                    const std::size_t end = metadata.find('\n', begin);
                    return std::string{
                        metadata.substr(
                            begin,
                            end == std::string_view::npos
                                ? std::string_view::npos
                                : end - begin)
                    };
                }

                position = metadata.find(needle, position + 1);
            }

            return std::nullopt;
        }


        [[nodiscard]]
        std::optional<std::size_t> positiveSize(
            const std::string_view text) noexcept
        {
            if (text.empty())
            {
                return std::nullopt;
            }

            std::size_t value{ 0 };
            const auto [end, error] = std::from_chars(
                text.data(),
                text.data() + text.size(),
                value);

            if (
                error != std::errc{}
                || end != text.data() + text.size()
                || value == 0)
            {
                return std::nullopt;
            }

            return value;
        }


        [[nodiscard]]
        std::optional<std::size_t> positiveArgument(
            const tools::ToolRequest& request,
            const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            if (found == request.arguments.end())
            {
                return std::nullopt;
            }

            return positiveSize(found->second);
        }


        [[nodiscard]]
        std::string normalizedPath(
            const std::string_view rawPath)
        {
            std::string normalized =
                std::filesystem::path{ rawPath }
                    .lexically_normal()
                    .generic_string();

#ifdef _WIN32
            std::transform(
                normalized.begin(),
                normalized.end(),
                normalized.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
#endif

            return normalized;
        }


        [[nodiscard]]
        std::optional<SourceRepairDiagnostic> parseDiagnostic(
            const std::string_view metadata)
        {
            if (!isSourceDiagnosticMetadata(metadata))
            {
                return std::nullopt;
            }

            const auto producer = lineValue(metadata, "producer_tool");
            const auto path = lineValue(metadata, "diagnostic_path");
            const auto rawLine = lineValue(metadata, "diagnostic_line");
            const auto severity = lineValue(metadata, "diagnostic_severity");

            if (
                !producer.has_value()
                || !path.has_value()
                || !rawLine.has_value()
                || !severity.has_value())
            {
                return std::nullopt;
            }

            const auto sourceLine = positiveSize(*rawLine);
            if (!sourceLine.has_value())
            {
                return std::nullopt;
            }

            std::size_t column{ 0 };
            if (const auto rawColumn = lineValue(metadata, "diagnostic_column");
                rawColumn.has_value() && !rawColumn->empty())
            {
                const auto parsedColumn = positiveSize(*rawColumn);
                if (parsedColumn.has_value())
                {
                    column = *parsedColumn;
                }
            }

            return SourceRepairDiagnostic{
                .producerTool = *producer,
                .path = *path,
                .line = *sourceLine,
                .column = column,
                .severity = *severity,
                .code = lineValue(metadata, "diagnostic_code").value_or("")
            };
        }


        [[nodiscard]]
        std::string decodeProtocolText(
            const std::string_view encoded)
        {
            std::string decoded;
            decoded.reserve(encoded.size());

            for (std::size_t index{ 0 }; index < encoded.size(); ++index)
            {
                const char character = encoded[index];
                if (character != '\\' || index + 1 >= encoded.size())
                {
                    decoded.push_back(character);
                    continue;
                }

                const char next = encoded[index + 1];
                switch (next)
                {
                case 'n': decoded.push_back('\n'); ++index; break;
                case 'r': decoded.push_back('\r'); ++index; break;
                case 't': decoded.push_back('\t'); ++index; break;
                case 's': decoded.push_back(' '); ++index; break;
                case '\\': decoded.push_back('\\'); ++index; break;
                default:
                    decoded.push_back('\\');
                    break;
                }
            }

            return decoded;
        }


        [[nodiscard]]
        std::string singleLinePreview(
            const std::string_view value,
            const std::size_t maximumCharacters)
        {
            std::string preview;
            preview.reserve((std::min)(value.size(), maximumCharacters));

            for (const char character : value)
            {
                if (preview.size() >= maximumCharacters)
                {
                    break;
                }

                if (character == '\r' || character == '\n' || character == '\t')
                {
                    preview.push_back(' ');
                }
                else if (std::iscntrl(static_cast<unsigned char>(character)) == 0)
                {
                    preview.push_back(character);
                }
            }

            if (value.size() > maximumCharacters)
            {
                preview += "...";
            }

            return preview;
        }


        [[nodiscard]]
        std::string replacementPreview(
            const SourceRepairPlan& plan)
        {
            constexpr std::size_t maximumLines{ 12 };
            constexpr std::size_t maximumLineCharacters{ 240 };

            const std::string decoded =
                decodeProtocolText(plan.replacementProtocolText);

            std::ostringstream text;
            std::size_t begin{ 0 };
            std::size_t shown{ 0 };
            std::size_t lineNumber = plan.startLine;

            while (begin <= decoded.size() && shown < maximumLines)
            {
                const std::size_t newline = decoded.find('\n', begin);
                std::string_view line = std::string_view{ decoded }.substr(
                    begin,
                    newline == std::string::npos
                        ? std::string_view::npos
                        : newline - begin);

                if (!line.empty() && line.back() == '\r')
                {
                    line.remove_suffix(1);
                }

                text
                    << "  "
                    << lineNumber
                    << "|"
                    << singleLinePreview(line, maximumLineCharacters)
                    << "\n";

                ++shown;
                ++lineNumber;

                if (newline == std::string::npos)
                {
                    break;
                }

                begin = newline + 1;
            }

            if (begin < decoded.size())
            {
                text << "  ... replacement preview truncated ...\n";
            }

            return text.str();
        }
    } // namespace


    bool isSourceDiagnosticMetadata(
        const std::string_view trustedMetadata) noexcept
    {
        return
            trustedMetadata.find("metadata_kind=source_diagnostic")
                != std::string_view::npos
            && trustedMetadata.find("operation_success=false")
                != std::string_view::npos;
    }


    bool sourceWindowCoversDiagnostic(
        const std::optional<tools::SourceWindowEvidence>& sourceWindow,
        const std::string_view diagnosticMetadata)
    {
        if (!sourceWindow.has_value())
        {
            return false;
        }

        const auto diagnostic = parseDiagnostic(diagnosticMetadata);
        if (!diagnostic.has_value())
        {
            return false;
        }

        if (
            normalizedPath(sourceWindow->path)
                != normalizedPath(diagnostic->path)
            || sourceWindow->lineCount == 0)
        {
            return false;
        }

        const std::size_t windowEnd =
            sourceWindow->startLine + sourceWindow->lineCount - 1u;

        return
            diagnostic->line >= sourceWindow->startLine
            && diagnostic->line <= windowEnd;
    }


    std::optional<SourceRepairPlan> buildSourceRepairPlan(
        const tools::ToolRequest& effectiveRequest,
        const std::optional<tools::SourceWindowEvidence>& sourceWindow,
        const std::string_view diagnosticMetadata)
    {
        if (
            effectiveRequest.toolId != "edit_text_file"
            || !sourceWindow.has_value())
        {
            return std::nullopt;
        }

        const auto operation = effectiveRequest.arguments.find("operation");
        const auto path = effectiveRequest.arguments.find("path");
        const auto digest = effectiveRequest.arguments.find("expected_digest");
        const auto replacement = effectiveRequest.arguments.find("replacement_text");

        if (
            operation == effectiveRequest.arguments.end()
            || operation->second != "replace_line_range"
            || path == effectiveRequest.arguments.end()
            || digest == effectiveRequest.arguments.end()
            || replacement == effectiveRequest.arguments.end())
        {
            return std::nullopt;
        }

        const auto startLine = positiveArgument(effectiveRequest, "start_line");
        const auto lineCount = positiveArgument(effectiveRequest, "line_count");
        if (!startLine.has_value() || !lineCount.has_value())
        {
            return std::nullopt;
        }

        if (
            normalizedPath(path->second) != normalizedPath(sourceWindow->path)
            || sourceWindow->lineCount == 0
            || sourceWindow->lineSha256s.size() != sourceWindow->lineCount)
        {
            return std::nullopt;
        }

        const std::size_t requestedEnd = *startLine + *lineCount - 1u;
        const std::size_t sourceEnd =
            sourceWindow->startLine + sourceWindow->lineCount - 1u;

        if (
            *startLine < sourceWindow->startLine
            || requestedEnd > sourceEnd)
        {
            return std::nullopt;
        }

        const std::size_t offset = *startLine - sourceWindow->startLine;
        const std::span<const std::string> selectedLineDigests{
            sourceWindow->lineSha256s.data() + offset,
            *lineCount
        };

        const std::string roseOwnedDigest =
            files::sourceWindowSha256FromLineDigests(selectedLineDigests);

        if (
            roseOwnedDigest.empty()
            || digest->second != roseOwnedDigest)
        {
            return std::nullopt;
        }

        SourceRepairPlan plan{
            .path = path->second,
            .startLine = *startLine,
            .lineCount = *lineCount,
            .expectedDigest = roseOwnedDigest,
            .replacementProtocolText = replacement->second,
            .diagnostic = std::nullopt
        };

        if (const auto diagnostic = parseDiagnostic(diagnosticMetadata);
            diagnostic.has_value()
            && normalizedPath(diagnostic->path) == normalizedPath(plan.path)
            && diagnostic->line >= sourceWindow->startLine
            && diagnostic->line <= sourceEnd)
        {
            plan.diagnostic = *diagnostic;
        }

        return plan;
    }


    std::string formatSourceRepairPlan(
        const SourceRepairPlan& plan)
    {
        std::ostringstream text;
        const std::size_t endLine = plan.startLine + plan.lineCount - 1u;

        text
            << "Source repair plan:\n"
            << "- file: " << plan.path << "\n"
            << "- lines: " << plan.startLine;

        if (endLine != plan.startLine)
        {
            text << "-" << endLine;
        }

        text
            << "\n"
            << "- preimage: Rose-observed source window (SHA-256 "
            << plan.expectedDigest.substr(0, (std::min)(std::size_t{ 12 }, plan.expectedDigest.size()))
            << "...)\n";

        if (plan.diagnostic.has_value())
        {
            text
                << "- why: "
                << plan.diagnostic->producerTool
                << " reported "
                << plan.diagnostic->severity;

            if (!plan.diagnostic->code.empty())
            {
                text << " " << plan.diagnostic->code;
            }

            text
                << " at line "
                << plan.diagnostic->line;

            if (plan.diagnostic->column > 0)
            {
                text << ", column " << plan.diagnostic->column;
            }

            text << ".\n";
        }
        else
        {
            text
                << "- why: apply the user-requested source-line change against "
                   "the exact source window Rose just observed.\n";
        }

        text
            << "- replacement preview:\n"
            << replacementPreview(plan);

        return text.str();
    }

} // namespace rose::agent
