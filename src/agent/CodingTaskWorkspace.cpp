#include "agent/CodingTaskWorkspace.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace rose::agent
{
    namespace
    {
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
                    return static_cast<char>(
                        std::tolower(character));
                });
#endif

            return normalized;
        }


        [[nodiscard]]
        std::optional<std::size_t> positiveArgument(
            const tools::ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

            if (
                found == request.arguments.end()
                || found->second.empty())
            {
                return std::nullopt;
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
                return std::nullopt;
            }

            return value;
        }


        [[nodiscard]]
        bool inclusiveRangeEnd(
            const std::size_t start,
            const std::size_t count,
            std::size_t& end) noexcept
        {
            if (
                start == 0
                || count == 0
                || count - 1u
                    > (std::numeric_limits<std::size_t>::max)() - start)
            {
                return false;
            }

            end =
                start
                + count
                - 1u;

            return true;
        }


        [[nodiscard]]
        bool validEvidence(
            const tools::SourceWindowEvidence& evidence) noexcept
        {
            std::size_t ignoredEnd{ 0 };

            return
                !evidence.path.empty()
                && !evidence.sha256.empty()
                && evidence.lineCount > 0
                && evidence.lineSha256s.size() == evidence.lineCount
                && inclusiveRangeEnd(
                    evidence.startLine,
                    evidence.lineCount,
                    ignoredEnd);
        }


        void invalidateSourceWindowsForPath(
            CodingTaskWorkspaceState& state,
            const std::string_view rawPath)
        {
            if (rawPath.empty())
            {
                return;
            }

            const std::string target =
                normalizedPath(
                    rawPath);

            std::erase_if(
                state.sourceWindows,
                [&](const tools::SourceWindowEvidence& evidence)
                {
                    return
                        normalizedPath(evidence.path)
                        == target;
                });
        }


        void rememberEditedPath(
            CodingTaskWorkspaceState& state,
            const std::string_view rawPath)
        {
            if (rawPath.empty())
            {
                return;
            }

            const std::string target =
                normalizedPath(
                    rawPath);

            std::erase_if(
                state.editedPaths,
                [&](const std::string& existing)
                {
                    return
                        normalizedPath(existing)
                        == target;
                });

            state.editedPaths.emplace_back(
                rawPath);

            if (
                state.editedPaths.size()
                > maximumRetainedEditedPaths)
            {
                state.editedPaths.erase(
                    state.editedPaths.begin());
            }
        }


        [[nodiscard]]
        std::string singleLineMetadataValue(
            const std::string_view value)
        {
            std::string sanitized;
            sanitized.reserve(
                value.size());

            for (const char character : value)
            {
                switch (character)
                {
                case '\r':
                case '\n':
                case '\t':
                    sanitized.push_back(' ');
                    break;

                default:
                    if (
                        std::iscntrl(
                            static_cast<unsigned char>(
                                character)) == 0)
                    {
                        sanitized.push_back(
                            character);
                    }
                    break;
                }
            }

            return sanitized;
        }
    } // namespace


    void observeCodingSourceWindow(
        CodingTaskWorkspaceState& state,
        const tools::SourceWindowEvidence& evidence)
    {
        if (!validEvidence(evidence))
        {
            return;
        }

        const std::string target =
            normalizedPath(
                evidence.path);

        // A repeated read of the exact same logical range supersedes its older
        // digest. Keep distinct ranges because one coding task may inspect a
        // declaration in a header and a separate implementation region later.
        std::erase_if(
            state.sourceWindows,
            [&](const tools::SourceWindowEvidence& existing)
            {
                return
                    normalizedPath(existing.path) == target
                    && existing.startLine == evidence.startLine
                    && existing.lineCount == evidence.lineCount;
            });

        state.sourceWindows.push_back(
            evidence);

        while (
            state.sourceWindows.size()
            > maximumRetainedSourceWindows)
        {
            state.sourceWindows.erase(
                state.sourceWindows.begin());
        }
    }


    std::optional<tools::SourceWindowEvidence> sourceWindowForRequest(
        const CodingTaskWorkspaceState& state,
        const tools::ToolRequest& request)
    {
        if (request.toolId != "edit_text_file")
        {
            return std::nullopt;
        }

        const auto operation =
            request.arguments.find(
                "operation");

        const auto path =
            request.arguments.find(
                "path");

        if (
            operation == request.arguments.end()
            || operation->second != "replace_line_range"
            || path == request.arguments.end()
            || path->second.empty())
        {
            return std::nullopt;
        }

        const auto startLine =
            positiveArgument(
                request,
                "start_line");

        const auto lineCount =
            positiveArgument(
                request,
                "line_count");

        if (
            !startLine.has_value()
            || !lineCount.has_value())
        {
            return std::nullopt;
        }

        std::size_t requestedEnd{ 0 };
        if (!inclusiveRangeEnd(
                *startLine,
                *lineCount,
                requestedEnd))
        {
            return std::nullopt;
        }

        const std::string requestedPath =
            normalizedPath(
                path->second);

        const tools::SourceWindowEvidence* best{
            nullptr
        };

        // Reverse iteration gives the newest observation priority when two
        // candidate windows have the same size.
        for (
            auto iterator = state.sourceWindows.rbegin();
            iterator != state.sourceWindows.rend();
            ++iterator)
        {
            const tools::SourceWindowEvidence& evidence =
                *iterator;

            if (
                !validEvidence(evidence)
                || normalizedPath(evidence.path)
                    != requestedPath)
            {
                continue;
            }

            std::size_t evidenceEnd{ 0 };
            if (!inclusiveRangeEnd(
                    evidence.startLine,
                    evidence.lineCount,
                    evidenceEnd))
            {
                continue;
            }

            if (
                *startLine < evidence.startLine
                || requestedEnd > evidenceEnd)
            {
                continue;
            }

            if (
                best == nullptr
                || evidence.lineCount < best->lineCount)
            {
                best = &evidence;
            }
        }

        if (best == nullptr)
        {
            return std::nullopt;
        }

        return *best;
    }


    void observeCodingMutation(
        CodingTaskWorkspaceState& state,
        const tools::ToolRequest& request,
        const tools::ToolResult& result)
    {
        if (!result.success)
        {
            return;
        }

        if (request.toolId == "edit_text_file")
        {
            const auto path =
                request.arguments.find(
                    "path");

            if (
                path != request.arguments.end()
                && !path->second.empty())
            {
                // Any successful edit makes every pre-edit window for this path
                // stale, even if the actual mutation touched a different range.
                invalidateSourceWindowsForPath(
                    state,
                    path->second);

                rememberEditedPath(
                    state,
                    path->second);
            }

            return;
        }

        if (request.toolId == "create_text_file"
            || request.toolId == "create_directory_with_text_file")
        {
            const auto path =
                request.arguments.find(
                    "path");

            if (
                path != request.arguments.end()
                && !path->second.empty())
            {
                rememberEditedPath(
                    state,
                    path->second);
            }

            return;
        }

        // These operations can rename/remove many paths at once or make previous
        // absolute-path provenance meaningless. Clear rather than trying to infer
        // which retained source windows survived.
        if (
            request.toolId == "move_path"
            || request.toolId == "recycle_path"
            || request.toolId == "batch_move_paths"
            || request.toolId == "apply_rename_plan")
        {
            state.sourceWindows.clear();
            state.editedPaths.clear();
        }
    }


    std::string formatCodingTaskWorkspaceMetadata(
        const CodingTaskWorkspaceState& state)
    {
        if (
            state.sourceWindows.empty()
            && state.editedPaths.empty())
        {
            return {};
        }

        std::ostringstream text;

        text
            << "metadata_kind=coding_task_workspace\n"
            << "retained_source_window_count="
            << state.sourceWindows.size()
            << "\n";

        for (
            std::size_t index{ 0 };
            index < state.sourceWindows.size();
            ++index)
        {
            const tools::SourceWindowEvidence& evidence =
                state.sourceWindows[index];

            text
                << "source_window_"
                << (index + 1u)
                << "_path="
                << singleLineMetadataValue(
                    evidence.path)
                << "\n"
                << "source_window_"
                << (index + 1u)
                << "_start_line="
                << evidence.startLine
                << "\n"
                << "source_window_"
                << (index + 1u)
                << "_line_count="
                << evidence.lineCount
                << "\n";
        }

        text
            << "edited_path_count="
            << state.editedPaths.size()
            << "\n";

        for (
            std::size_t index{ 0 };
            index < state.editedPaths.size();
            ++index)
        {
            text
                << "edited_path_"
                << (index + 1u)
                << "="
                << singleLineMetadataValue(
                    state.editedPaths[index])
                << "\n";
        }

        text
            << "workspace_semantics=Retained source windows were observed earlier "
               "in this same bounded run and remain eligible for provenance binding "
               "only until that exact path is mutated.";

        return text.str();
    }

} // namespace rose::agent
