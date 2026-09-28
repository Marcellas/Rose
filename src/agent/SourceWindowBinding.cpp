#include "agent/SourceWindowBinding.h"

#include "files/SourceWindowDigest.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace rose::agent
{
    namespace
    {
        [[nodiscard]]
        std::optional<std::size_t> positiveIntegerArgument(
            const tools::ToolRequest& request,
            const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            if (found == request.arguments.end() || found->second.empty())
            {
                return std::nullopt;
            }

            std::size_t value{ 0 };
            const char* first = found->second.data();
            const char* last = first + found->second.size();
            const auto [end, error] = std::from_chars(first, last, value);

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
        std::string normalizedPathForComparison(
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
    } // namespace


    tools::ToolRequest bindSourceWindowEvidence(
        const tools::ToolRequest& requested,
        const std::optional<tools::SourceWindowEvidence>& evidence)
    {
        tools::ToolRequest bound = requested;

        if (!evidence.has_value() || requested.toolId != "edit_text_file")
        {
            return bound;
        }

        const auto operation = requested.arguments.find("operation");
        const auto path = requested.arguments.find("path");

        if (
            operation == requested.arguments.end()
            || operation->second != "replace_line_range"
            || path == requested.arguments.end())
        {
            return bound;
        }

        const std::optional<std::size_t> startLine =
            positiveIntegerArgument(requested, "start_line");
        const std::optional<std::size_t> lineCount =
            positiveIntegerArgument(requested, "line_count");

        if (
            !startLine.has_value()
            || !lineCount.has_value()
            || normalizedPathForComparison(path->second)
                != normalizedPathForComparison(evidence->path)
            || evidence->lineSha256s.size() != evidence->lineCount
            || evidence->lineCount == 0)
        {
            return bound;
        }

        const std::size_t requestedEnd =
            *startLine + *lineCount - 1u;
        const std::size_t evidenceEnd =
            evidence->startLine + evidence->lineCount - 1u;

        if (
            *startLine < evidence->startLine
            || requestedEnd > evidenceEnd)
        {
            return bound;
        }

        const std::size_t offset =
            *startLine - evidence->startLine;

        const std::span<const std::string> selectedLineDigests{
            evidence->lineSha256s.data() + offset,
            *lineCount
        };

        const std::string selectedDigest =
            files::sourceWindowSha256FromLineDigests(
                selectedLineDigests);

        if (selectedDigest.empty())
        {
            return bound;
        }

        // The latest Rose-owned read is a stronger preimage binding than
        // model-echoed source text. Any contiguous subrange of that exact observed
        // window can be rebound from per-line hashes without retaining raw source
        // bytes in trusted state. The mutation service independently recomputes
        // and verifies the resulting digest against the current file contents.
        bound.arguments.erase("expected_text");
        bound.arguments.insert_or_assign(
            "expected_digest",
            selectedDigest);

        return bound;
    }

} // namespace rose::agent
