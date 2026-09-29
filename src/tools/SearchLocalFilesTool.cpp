#include "tools/SearchLocalFilesTool.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

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

        [[nodiscard]] bool searchableText(const std::filesystem::path& path)
        {
            const std::string extension = lowerAscii(path.extension().string());
            static constexpr std::string_view extensions[] = {
                ".txt", ".md", ".log", ".csv", ".tsv", ".json", ".jsonl",
                ".xml", ".yaml", ".yml", ".toml", ".ini", ".cfg",
                ".cpp", ".c", ".h", ".hpp", ".cs", ".py", ".js",
                ".ts", ".tsx", ".html", ".css", ".rs", ".go",
                ".java", ".kt", ".sql", ".cmake", ".ps1", ".sh",
                ".tex", ".rst"
            };
            return std::find(std::begin(extensions), std::end(extensions), extension)
                    != std::end(extensions)
                || lowerAscii(path.filename().string()) == "cmakelists.txt";
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
            .description = "Search filenames and small text/source contents below one exact absolute directory. Bounded to 5000 entries, depth 8, 32 MiB scanned text, 60 matches, and 24 KiB output. Does not follow symlinks or use the network. Query is a literal, ASCII case-insensitive substring.",
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
        if (!root.is_absolute() || query.empty() || query.size() > 256)
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
            if (lowerAscii(path.filename().string()).find(query) != std::string::npos)
            {
                matches << pathText << " [filename]\n";
                ++found;
                if (matches.tellp() >= static_cast<std::streamoff>(maximumOutput))
                {
                    partial = true;
                    break;
                }
            }

            if (found >= maximumMatches
                || matches.tellp() >= static_cast<std::streamoff>(maximumOutput)
                || !searchableText(path)) continue;
            const auto size = entry.file_size(error);
            if (error || size > maximumTextFileBytes)
            {
                error.clear();
                continue;
            }
            if (size > maximumTotalTextBytes - scannedTextBytes)
            {
                partial = true;
                break;
            }
            scannedTextBytes += size;

            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                partial = true;
                continue;
            }
            std::string line;
            std::size_t lineNumber{ 0 };
            while (std::getline(input, line) && found < maximumMatches)
            {
                ++lineNumber;
                if (line.find('\0') != std::string::npos) break;
                if (lowerAscii(line).find(query) == std::string::npos) continue;
                matches << pathText << ':' << lineNumber << ": " << excerpt(line) << '\n';
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
            .responseMode = ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };
    }
}
