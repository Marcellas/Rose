#include "development/DiagnosticExtraction.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace rose::development
{
    namespace
    {
        struct ParsedDiagnostic
        {
            std::string rawPath;
            std::size_t line{ 0 };
            std::size_t column{ 0 };
            std::string severity;
            std::string code;
            std::string message;
        };

        [[nodiscard]]
        std::string_view trimView(std::string_view value) noexcept
        {
            while (!value.empty()
                && std::isspace(static_cast<unsigned char>(value.front())) != 0)
            {
                value.remove_prefix(1);
            }

            while (!value.empty()
                && std::isspace(static_cast<unsigned char>(value.back())) != 0)
            {
                value.remove_suffix(1);
            }

            return value;
        }

        [[nodiscard]]
        bool parsePositiveSize(
            const std::string_view text,
            std::size_t& value) noexcept
        {
            if (text.empty()) return false;

            std::size_t parsed{};
            const auto [end, error] = std::from_chars(
                text.data(), text.data() + text.size(), parsed);
            if (error != std::errc{}
                || end != text.data() + text.size()
                || parsed == 0)
            {
                return false;
            }

            value = parsed;
            return true;
        }

        [[nodiscard]]
        std::string lowerAscii(std::string value)
        {
            std::transform(
                value.begin(), value.end(), value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        [[nodiscard]]
        bool startsWithSeverity(
            const std::string_view rest,
            std::string& severity,
            std::string_view& afterSeverity)
        {
            static constexpr std::pair<std::string_view, std::string_view> patterns[]{
                { "fatal error", "fatal_error" },
                { "error", "error" },
                { "warning", "warning" },
                { "note", "note" },
                { "failure", "test_failure" }
            };

            const std::string lower = lowerAscii(std::string{ rest });
            for (const auto& [prefix, normalized] : patterns)
            {
                if (!lower.starts_with(prefix)) continue;

                const std::size_t prefixSize = prefix.size();
                if (lower.size() > prefixSize)
                {
                    const char next = lower[prefixSize];
                    if (next != ':'
                        && std::isspace(static_cast<unsigned char>(next)) == 0)
                    {
                        continue;
                    }
                }

                severity = std::string{ normalized };
                afterSeverity = trimView(rest.substr(prefixSize));
                if (!afterSeverity.empty() && afterSeverity.front() == ':')
                {
                    afterSeverity.remove_prefix(1);
                    afterSeverity = trimView(afterSeverity);
                }
                return true;
            }

            return false;
        }

        [[nodiscard]]
        std::pair<std::string, std::string> splitCodeAndMessage(
            const std::string_view text)
        {
            const std::size_t colon = text.find(':');
            if (colon == std::string_view::npos)
            {
                return { {}, std::string{ trimView(text) } };
            }

            const std::string_view first = trimView(text.substr(0, colon));
            const bool codeLike = !first.empty()
                && first.size() <= 32
                && std::none_of(
                    first.begin(), first.end(),
                    [](const char character)
                    {
                        return std::isspace(
                            static_cast<unsigned char>(character)) != 0;
                    });

            if (!codeLike)
            {
                return { {}, std::string{ trimView(text) } };
            }

            return {
                std::string{ first },
                std::string{ trimView(text.substr(colon + 1)) }
            };
        }

        [[nodiscard]]
        std::optional<ParsedDiagnostic> parseMsvcLine(
            const std::string_view line)
        {
            const std::size_t close = line.rfind("):");
            if (close == std::string_view::npos) return std::nullopt;

            const std::size_t open = line.rfind('(', close);
            if (open == std::string_view::npos || open == 0) return std::nullopt;

            const std::string_view location =
                trimView(line.substr(open + 1, close - open - 1));
            const std::size_t comma = location.find(',');

            std::size_t sourceLine{};
            std::size_t sourceColumn{};
            if (comma == std::string_view::npos)
            {
                if (!parsePositiveSize(location, sourceLine)) return std::nullopt;
            }
            else
            {
                if (!parsePositiveSize(trimView(location.substr(0, comma)), sourceLine)
                    || !parsePositiveSize(trimView(location.substr(comma + 1)), sourceColumn))
                {
                    return std::nullopt;
                }
            }

            std::string_view rest = trimView(line.substr(close + 2));
            std::string severity;
            std::string_view afterSeverity;
            if (!startsWithSeverity(rest, severity, afterSeverity))
                return std::nullopt;

            auto [code, message] = splitCodeAndMessage(afterSeverity);
            return ParsedDiagnostic{
                .rawPath = std::string{ trimView(line.substr(0, open)) },
                .line = sourceLine,
                .column = sourceColumn,
                .severity = std::move(severity),
                .code = std::move(code),
                .message = std::move(message)
            };
        }

        [[nodiscard]]
        std::optional<ParsedDiagnostic> parseColonLocationLine(
            const std::string_view line)
        {
            static constexpr std::pair<std::string_view, std::string_view> markers[]{
                { ": fatal error:", "fatal_error" },
                { ": error:", "error" },
                { ": warning:", "warning" },
                { ": note:", "note" },
                { ": Failure", "test_failure" },
                { ": failure:", "test_failure" },
                { ": FAILED:", "test_failure" },
                { ": failed:", "test_failure" }
            };

            std::size_t markerPosition = std::string_view::npos;
            std::string severity;
            std::size_t markerSize{};

            for (const auto& [marker, normalized] : markers)
            {
                const std::size_t found = line.find(marker);
                if (found != std::string_view::npos
                    && (markerPosition == std::string_view::npos
                        || found < markerPosition))
                {
                    markerPosition = found;
                    severity = std::string{ normalized };
                    markerSize = marker.size();
                }
            }

            if (markerPosition == std::string_view::npos) return std::nullopt;

            const std::string_view location =
                trimView(line.substr(0, markerPosition));
            const std::size_t lastColon = location.rfind(':');
            if (lastColon == std::string_view::npos) return std::nullopt;

            std::size_t lastNumber{};
            if (!parsePositiveSize(
                    trimView(location.substr(lastColon + 1)),
                    lastNumber))
            {
                return std::nullopt;
            }

            std::size_t sourceLine = lastNumber;
            std::size_t sourceColumn{};
            std::size_t pathEnd = lastColon;

            const std::size_t previousColon =
                location.rfind(':', lastColon == 0 ? 0 : lastColon - 1);
            if (previousColon != std::string_view::npos)
            {
                std::size_t previousNumber{};
                if (parsePositiveSize(
                        trimView(location.substr(
                            previousColon + 1,
                            lastColon - previousColon - 1)),
                        previousNumber))
                {
                    sourceLine = previousNumber;
                    sourceColumn = lastNumber;
                    pathEnd = previousColon;
                }
            }

            const std::string_view rawPath =
                trimView(location.substr(0, pathEnd));
            if (rawPath.empty()) return std::nullopt;

            const std::string_view messageText = trimView(
                line.substr(markerPosition + markerSize));

            return ParsedDiagnostic{
                .rawPath = std::string{ rawPath },
                .line = sourceLine,
                .column = sourceColumn,
                .severity = std::move(severity),
                .code = {},
                .message = std::string{ messageText }
            };
        }

        [[nodiscard]]
        std::optional<ParsedDiagnostic> parseCMakeLine(
            const std::string_view line)
        {
            static constexpr std::pair<std::string_view, std::string_view> prefixes[]{
                { "CMake Error at ", "error" },
                { "CMake Warning at ", "warning" }
            };

            for (const auto& [prefix, severity] : prefixes)
            {
                if (!line.starts_with(prefix)) continue;

                const std::string_view locationAndRest = line.substr(prefix.size());
                std::size_t separator = locationAndRest.find(" (");
                if (separator == std::string_view::npos)
                {
                    separator = locationAndRest.find(':');
                    while (separator != std::string_view::npos)
                    {
                        std::size_t candidateLine{};
                        const std::size_t nextSpace = locationAndRest.find(' ', separator + 1);
                        const std::string_view number = trimView(
                            locationAndRest.substr(
                                separator + 1,
                                nextSpace == std::string_view::npos
                                    ? std::string_view::npos
                                    : nextSpace - separator - 1));
                        if (parsePositiveSize(number, candidateLine)) break;
                        separator = locationAndRest.find(':', separator + 1);
                    }
                }

                if (separator == std::string_view::npos) return std::nullopt;

                // Work backward from " (command)" or another suffix to locate the
                // final :<line> segment without confusing a Windows drive colon.
                const std::string_view location = trimView(
                    locationAndRest.substr(0, separator));
                const std::size_t colon = location.rfind(':');
                if (colon == std::string_view::npos) return std::nullopt;

                std::size_t sourceLine{};
                if (!parsePositiveSize(trimView(location.substr(colon + 1)), sourceLine))
                    return std::nullopt;

                return ParsedDiagnostic{
                    .rawPath = std::string{ trimView(location.substr(0, colon)) },
                    .line = sourceLine,
                    .column = 0,
                    .severity = std::string{ severity },
                    .code = "CMake",
                    .message = std::string{ trimView(locationAndRest.substr(separator)) }
                };
            }

            return std::nullopt;
        }

        [[nodiscard]]
        bool componentEqual(
            const std::filesystem::path& left,
            const std::filesystem::path& right)
        {
#ifdef _WIN32
            return lowerAscii(left.string()) == lowerAscii(right.string());
#else
            return left == right;
#endif
        }

        [[nodiscard]]
        bool pathWithin(
            const std::filesystem::path& child,
            const std::filesystem::path& root)
        {
            auto childIt = child.begin();
            const auto childEnd = child.end();
            for (auto rootIt = root.begin(); rootIt != root.end(); ++rootIt)
            {
                if (childIt == childEnd || !componentEqual(*childIt, *rootIt))
                    return false;
                ++childIt;
            }
            return true;
        }

        [[nodiscard]]
        std::optional<std::filesystem::path> groundedProjectFile(
            const std::string_view rawPath,
            const std::filesystem::path& sourceDirectory)
        {
            std::string cleaned{ trimView(rawPath) };
            if (cleaned.size() >= 2
                && ((cleaned.front() == '"' && cleaned.back() == '"')
                    || (cleaned.front() == '\'' && cleaned.back() == '\'')))
            {
                cleaned = cleaned.substr(1, cleaned.size() - 2);
            }
            if (cleaned.empty()) return std::nullopt;

            std::error_code error;
            const std::filesystem::path canonicalRoot =
                std::filesystem::weakly_canonical(sourceDirectory, error);
            if (error) return std::nullopt;

            std::vector<std::filesystem::path> candidates;
            const std::filesystem::path raw{ cleaned };
            if (raw.is_absolute())
            {
                candidates.push_back(raw);
            }
            else
            {
                candidates.push_back(sourceDirectory / raw);
                candidates.push_back(sourceDirectory / "build" / raw);
            }

            for (const auto& candidate : candidates)
            {
                error.clear();
                const std::filesystem::path canonicalCandidate =
                    std::filesystem::weakly_canonical(candidate, error);
                if (error || !pathWithin(canonicalCandidate, canonicalRoot))
                    continue;

                error.clear();
                if (!std::filesystem::is_regular_file(canonicalCandidate, error)
                    || error)
                {
                    continue;
                }

                return canonicalCandidate;
            }

            return std::nullopt;
        }

        [[nodiscard]]
        std::string_view stripCTestOutputPrefix(
            std::string_view line) noexcept
        {
            std::size_t index{ 0 };
            while (index < line.size()
                && std::isdigit(static_cast<unsigned char>(line[index])) != 0)
            {
                ++index;
            }

            if (index == 0 || index >= line.size() || line[index] != ':')
                return line;

            ++index;
            if (index >= line.size()
                || std::isspace(static_cast<unsigned char>(line[index])) == 0)
            {
                return line;
            }

            while (index < line.size()
                && std::isspace(static_cast<unsigned char>(line[index])) != 0)
            {
                ++index;
            }
            return line.substr(index);
        }


        [[nodiscard]]
        int severityRank(const std::string_view severity) noexcept
        {
            if (severity == "fatal_error") return 0;
            if (severity == "error") return 1;
            if (severity == "test_failure") return 2;
            if (severity == "warning") return 3;
            return 4;
        }

        [[nodiscard]]
        std::string sanitizeMessage(std::string value)
        {
            for (char& character : value)
            {
                if (character == '\r' || character == '\n' || character == '\0')
                    character = ' ';
            }
            if (value.size() > 512) value.resize(512);
            return value;
        }
    }

    std::vector<SourceDiagnostic> extractSourceDiagnostics(
        const std::string_view output,
        const std::filesystem::path& sourceDirectory,
        const std::size_t maximumDiagnostics)
    {
        std::vector<SourceDiagnostic> diagnostics;
        if (maximumDiagnostics == 0 || output.empty()) return diagnostics;

        std::size_t offset{};
        while (offset < output.size())
        {
            const std::size_t newline = output.find('\n', offset);
            std::string_view line = output.substr(
                offset,
                newline == std::string_view::npos
                    ? std::string_view::npos
                    : newline - offset);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            line = stripCTestOutputPrefix(line);

            std::optional<ParsedDiagnostic> parsed = parseMsvcLine(line);
            if (!parsed.has_value()) parsed = parseColonLocationLine(line);
            if (!parsed.has_value()) parsed = parseCMakeLine(line);

            if (parsed.has_value() && parsed->line > 0)
            {
                const std::optional<std::filesystem::path> path =
                    groundedProjectFile(parsed->rawPath, sourceDirectory);
                if (path.has_value())
                {
                    SourceDiagnostic diagnostic{
                        .path = *path,
                        .line = parsed->line,
                        .column = parsed->column,
                        .severity = std::move(parsed->severity),
                        .code = std::move(parsed->code),
                        .message = sanitizeMessage(std::move(parsed->message))
                    };

                    const auto duplicate = std::find_if(
                        diagnostics.begin(), diagnostics.end(),
                        [&](const SourceDiagnostic& existing)
                        {
                            return existing.path == diagnostic.path
                                && existing.line == diagnostic.line
                                && existing.column == diagnostic.column
                                && existing.severity == diagnostic.severity
                                && existing.code == diagnostic.code;
                        });

                    if (duplicate == diagnostics.end())
                        diagnostics.push_back(std::move(diagnostic));
                }
            }

            if (newline == std::string_view::npos) break;
            offset = newline + 1;
        }

        std::stable_sort(
            diagnostics.begin(), diagnostics.end(),
            [](const SourceDiagnostic& left, const SourceDiagnostic& right)
            {
                return severityRank(left.severity) < severityRank(right.severity);
            });

        if (diagnostics.size() > maximumDiagnostics)
            diagnostics.resize(maximumDiagnostics);

        return diagnostics;
    }


    std::string buildTrustedDiagnosticMetadata(
        const std::string_view producerToolId,
        const bool operationSucceeded,
        const std::span<const SourceDiagnostic> diagnostics)
    {
        if (operationSucceeded || diagnostics.empty()) return {};

        const SourceDiagnostic& primary = diagnostics.front();
        std::ostringstream metadata;
        metadata
            << "metadata_kind=source_diagnostic\n"
            << "producer_tool=" << producerToolId << '\n'
            << "operation_success=false\n"
            << "diagnostic_path=" << primary.path.string() << '\n'
            << "diagnostic_line=" << primary.line << '\n'
            << "diagnostic_column=" << primary.column << '\n'
            << "diagnostic_severity=" << primary.severity << '\n'
            << "diagnostic_code=" << primary.code << '\n'
            << "suggested_read_start_line="
            << suggestedDiagnosticStartLine(primary.line) << '\n'
            << "suggested_read_line_count="
            << suggestedDiagnosticLineCount();
        return metadata.str();
    }

    std::size_t suggestedDiagnosticStartLine(
        const std::size_t diagnosticLine) noexcept
    {
        constexpr std::size_t kContextBefore = 30;
        if (diagnosticLine <= kContextBefore + 1) return 1;
        return diagnosticLine - kContextBefore;
    }
}
