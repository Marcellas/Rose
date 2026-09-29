#include "tools/SearchLocalFilesTool.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]] std::string lowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        [[nodiscard]] bool separator(const char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '_' || c == '-';
        }

        [[nodiscard]] std::string withoutSeparators(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (const char c : text)
            {
                if (!separator(c)) result.push_back(c);
            }
            return result;
        }

        enum class MatchKind { None, Literal, SeparatorFolded };

        [[nodiscard]] MatchKind matchText(std::string text,
            std::string_view query, std::string_view foldedQuery)
        {
            text = lowerAscii(std::move(text));
            if (text.find(query) != std::string::npos) return MatchKind::Literal;
            if (query != foldedQuery
                && withoutSeparators(text).find(foldedQuery) != std::string::npos)
                return MatchKind::SeparatorFolded;
            return MatchKind::None;
        }

        struct TextProbe
        {
            bool text{ false };
            std::size_t bytesRead{ 0 };
        };

        [[nodiscard]] TextProbe probePlainText(std::ifstream& input)
        {
            std::array<char, 4096> sample{};
            input.read(sample.data(), static_cast<std::streamsize>(sample.size()));
            const auto count = static_cast<std::size_t>(input.gcount());
            input.clear();
            input.seekg(0, std::ios::beg);
            if (!input) return { false, count };

            // UTF-16 requires decoding; a byte-wise substring search cannot
            // reliably report whether it contains this literal query.
            if (count >= 2 && ((static_cast<unsigned char>(sample[0]) == 0xff
                    && static_cast<unsigned char>(sample[1]) == 0xfe)
                || (static_cast<unsigned char>(sample[0]) == 0xfe
                    && static_cast<unsigned char>(sample[1]) == 0xff)))
                return { false, count };
            if (count >= 5 && std::string_view(sample.data(), 5) == "%PDF-")
                return { false, count };
            if (count >= 4 && sample[0] == 'P' && sample[1] == 'K'
                && static_cast<unsigned char>(sample[2]) == 3
                && static_cast<unsigned char>(sample[3]) == 4)
                return { false, count };

            std::size_t controls{ 0 };
            for (std::size_t i = 0; i < count; ++i)
            {
                const unsigned char byte = static_cast<unsigned char>(sample[i]);
                if (byte == 0) return { false, count };
                if ((byte < 32 && byte != '\t' && byte != '\n'
                        && byte != '\r' && byte != '\f') || byte == 127)
                    ++controls;
            }
            return { count == 0 || controls * 100 <= count, count };
        }

        [[nodiscard]] std::string excerpt(std::string_view line)
        {
            constexpr std::size_t maximum{ 180 };
            std::string result(line.substr(0, maximum));
            for (char& c : result)
            {
                if (c == '\r' || c == '\n' || c == '\t') c = ' ';
            }
            if (line.size() > maximum) result += "...";
            return result;
        }
    }

    SearchLocalFilesTool::SearchLocalFilesTool()
        : descriptor_{
            .id = "search_local_files",
            .displayName = "Search Offline Files",
            .description = "Search filenames and likely plain-text contents regardless of extension below one exact absolute directory. Bounded to 5000 entries, depth 8, 1 MiB per file, 32 MiB read, 60 matches, and 24 KiB output. Binary files remain searchable by name. Does not follow symlinks or use the network. ASCII case-insensitive; also matches code names after folding spaces, underscores, and hyphens in a query.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                { .name = "path", .description = "Absolute directory to search.", .type = ToolValueType::String, .required = true },
                { .name = "query", .description = "Literal filename or text to find.", .type = ToolValueType::String, .required = true }
            }
        }
    {
    }

    const ToolDescriptor& SearchLocalFilesTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult SearchLocalFilesTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id || request.arguments.size() != 2
            || !request.arguments.contains("path") || !request.arguments.contains("query"))
        {
            throw std::invalid_argument{ "search_local_files requires only path and query." };
        }

        const std::filesystem::path root{ request.arguments.at("path") };
        const std::string query = lowerAscii(request.arguments.at("query"));
        const std::string foldedQuery = withoutSeparators(query);
        if (!root.is_absolute() || foldedQuery.empty() || query.size() > 256)
        {
            throw std::invalid_argument{ "Search requires an absolute directory and a 1..256 byte query." };
        }

        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || error)
        {
            throw std::invalid_argument{ "Search root must be an existing directory." };
        }

        constexpr std::size_t maximumEntries{ 5000 };
        constexpr std::size_t maximumMatches{ 60 };
        constexpr std::size_t maximumOutput{ 24u * 1024u };
        constexpr std::uintmax_t maximumTextFileBytes{ 1024u * 1024u };
        constexpr std::uintmax_t maximumTotalTextBytes{ 32u * 1024u * 1024u };
        std::filesystem::recursive_directory_iterator it{
            root, std::filesystem::directory_options::skip_permission_denied, error
        };
        if (error) throw std::runtime_error{ "Could not start offline search." };

        std::ostringstream matches;
        std::size_t visited{ 0 };
        std::size_t found{ 0 };
        std::uintmax_t scannedTextBytes{ 0 };
        bool partial{ false };
        const std::filesystem::recursive_directory_iterator end;

        for (; it != end && visited < maximumEntries && found < maximumMatches;
             it.increment(error))
        {
            if (error) { partial = true; error.clear(); continue; }
            ++visited;
            const auto& entry = *it;
            const auto status = entry.symlink_status(error);
            if (error) { partial = true; error.clear(); continue; }
            if (std::filesystem::is_symlink(status))
            {
                it.disable_recursion_pending();
                continue;
            }
            if (it.depth() >= 7) it.disable_recursion_pending();
            if (!std::filesystem::is_regular_file(status)) continue;

            const std::filesystem::path path = entry.path();
            const std::string pathText = path.string();
            const MatchKind filenameMatch = matchText(path.filename().string(),
                query, foldedQuery);
            if (filenameMatch != MatchKind::None)
            {
                matches << pathText << (filenameMatch == MatchKind::Literal
                    ? " [filename]\n" : " [filename, separator-folded]\n");
                ++found;
                if (matches.tellp() >= static_cast<std::streamoff>(maximumOutput))
                {
                    partial = true;
                    break;
                }
            }

            if (found >= maximumMatches
                || matches.tellp() >= static_cast<std::streamoff>(maximumOutput)) continue;
            const auto size = entry.file_size(error);
            if (error || size > maximumTextFileBytes)
            {
                error.clear();
                continue;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                partial = true;
                continue;
            }
            const std::uintmax_t probeBytes = std::min<std::uintmax_t>(size, 4096);
            if (probeBytes > maximumTotalTextBytes - scannedTextBytes)
            {
                partial = true;
                break;
            }
            const TextProbe probe = probePlainText(input);
            scannedTextBytes += probe.bytesRead;
            if (!probe.text) continue;
            if (size > maximumTotalTextBytes - scannedTextBytes)
            {
                partial = true;
                break;
            }
            scannedTextBytes += size;
            std::string line;
            std::size_t lineNumber{ 0 };
            while (std::getline(input, line) && found < maximumMatches)
            {
                ++lineNumber;
                if (line.find('\0') != std::string::npos) break;
                const MatchKind lineMatch = matchText(line, query, foldedQuery);
                if (lineMatch == MatchKind::None) continue;
                matches << pathText << ':' << lineNumber << ": " << excerpt(line)
                    << (lineMatch == MatchKind::SeparatorFolded
                        ? " [separator-folded]" : "") << '\n';
                ++found;
                if (matches.tellp() >= static_cast<std::streamoff>(maximumOutput)) break;
            }
            if (matches.tellp() >= static_cast<std::streamoff>(maximumOutput)) break;
        }

        partial = partial || (it != end);
        return ToolResult{
            .success = true,
            .message = "Offline search under " + root.string() + " for '"
                + request.arguments.at("query") + "': " + std::to_string(found)
                + " match(es), " + std::to_string(visited) + " entries visited"
                + (partial ? " (bounded/partial).\n" : ".\n")
                + (found ? matches.str() : "No matches in the searched scope.\n"),
            .trustedMetadata = {},
            .sourceWindowEvidence = std::nullopt,
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
