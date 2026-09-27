#include "knowledge/Utf8TextProjectContentReader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rose::knowledge
{
    namespace
    {
        [[nodiscard]]
        std::string lowerAscii(
            std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }


        [[nodiscard]]
        bool endsWith(
            const std::string_view value,
            const std::string_view suffix) noexcept
        {
            return value.size() >= suffix.size()
                && value.substr(value.size() - suffix.size()) == suffix;
        }


        [[nodiscard]]
        bool isValidUtf8(
            const std::string_view bytes) noexcept
        {
            const auto* data =
                reinterpret_cast<const unsigned char*>(bytes.data());
            std::size_t index{ 0 };

            while (index < bytes.size())
            {
                const unsigned char lead = data[index];
                if (lead <= 0x7Fu)
                {
                    ++index;
                    continue;
                }

                std::size_t continuationCount{ 0 };
                std::uint32_t codePoint{ 0 };
                if ((lead & 0xE0u) == 0xC0u)
                {
                    continuationCount = 1;
                    codePoint = lead & 0x1Fu;
                    if (codePoint == 0) return false;
                }
                else if ((lead & 0xF0u) == 0xE0u)
                {
                    continuationCount = 2;
                    codePoint = lead & 0x0Fu;
                }
                else if ((lead & 0xF8u) == 0xF0u)
                {
                    continuationCount = 3;
                    codePoint = lead & 0x07u;
                }
                else
                {
                    return false;
                }

                if (index + continuationCount >= bytes.size()) return false;
                for (std::size_t continuation = 0;
                     continuation < continuationCount;
                     ++continuation)
                {
                    const unsigned char value = data[index + 1 + continuation];
                    if ((value & 0xC0u) != 0x80u) return false;
                    codePoint = (codePoint << 6u) | (value & 0x3Fu);
                }

                if ((continuationCount == 1 && codePoint < 0x80u)
                    || (continuationCount == 2 && codePoint < 0x800u)
                    || (continuationCount == 3 && codePoint < 0x10000u)
                    || codePoint > 0x10FFFFu
                    || (codePoint >= 0xD800u && codePoint <= 0xDFFFu))
                {
                    return false;
                }

                index += continuationCount + 1;
            }
            return true;
        }
    }


    bool Utf8TextProjectContentReader::supports(
        const std::filesystem::path& path) const noexcept
    {
        // These are formats Rose can safely treat as plain source text without a
        // format-specific parser. Structured/binary documents deliberately belong
        // to future readers instead of being guessed here.
        static constexpr std::string_view extensions[]{
            ".txt", ".md", ".markdown", ".rst", ".adoc", ".csv", ".tsv",
            ".log", ".json", ".jsonc", ".xml", ".yaml", ".yml", ".ini",
            ".cfg", ".conf", ".toml", ".env",

            // C/C++ and Visual Studio/native-project files.
            ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
            ".inl", ".ipp", ".ixx", ".cppm", ".rc", ".def", ".idl",
            ".natvis", ".sln", ".slnx", ".vcxproj", ".props", ".targets",
            ".filters", ".manifest", ".resx",

            // .NET and general-purpose languages.
            ".cs", ".csproj", ".fs", ".fsx", ".fsproj", ".vb", ".vbproj",
            ".java", ".kt", ".kts", ".go", ".rs", ".swift", ".m", ".mm",
            ".py", ".pyi", ".pyx", ".js", ".mjs", ".cjs", ".ts", ".tsx",
            ".jsx", ".lua", ".rb", ".php", ".pl", ".pm", ".r", ".sql",
            ".dart", ".scala", ".groovy", ".gvy", ".gradle",

            // Shell/build/web/configuration languages.
            ".sh", ".bash", ".zsh", ".fish", ".ps1", ".psm1", ".psd1",
            ".bat", ".cmd", ".asm", ".s", ".cmake", ".make", ".mk",
            ".html", ".htm", ".css", ".scss", ".sass", ".less", ".vue",
            ".svelte", ".graphql", ".gql", ".proto", ".dockerfile"
        };

        const std::string extension = lowerAscii(path.extension().string());
        if (!extension.empty()
            && std::find(
                std::begin(extensions),
                std::end(extensions),
                extension) != std::end(extensions))
        {
            return true;
        }

        const std::string filename = lowerAscii(path.filename().string());
        static constexpr std::string_view exactNames[]{
            "cmakelists.txt", "makefile", "gnumakefile", "dockerfile",
            "readme", "license", "copying", ".env", ".editorconfig", ".gitignore",
            ".gitattributes", ".gitmodules", ".clang-format", ".clang-tidy"
        };
        if (std::find(std::begin(exactNames), std::end(exactNames), filename)
            != std::end(exactNames))
        {
            return true;
        }

        // Multi-dot Visual Studio project metadata reports only the last suffix
        // through path.extension(), so recognize the full filename as well.
        return endsWith(filename, ".vcxproj.filters")
            || endsWith(filename, ".vcxproj.user")
            || endsWith(filename, ".csproj.user");
    }


    ExtractedProjectContent Utf8TextProjectContentReader::read(
        const std::filesystem::path& path,
        const std::uintmax_t sourceBytes) const
    {
        if (sourceBytes
            > static_cast<std::uintmax_t>(
                (std::numeric_limits<std::size_t>::max)()))
        {
            throw std::runtime_error{
                "File is too large to address safely."
            };
        }

        std::ifstream input{ path, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "Could not open file." };
        }

        std::string bytes(static_cast<std::size_t>(sourceBytes), '\0');
        if (!bytes.empty())
        {
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
            {
                throw std::runtime_error{
                    "Could not read the complete file."
                };
            }
        }

        if (bytes.find('\0') != std::string::npos)
        {
            throw std::runtime_error{
                "File appears to be binary."
            };
        }

        if (bytes.size() >= 3
            && static_cast<unsigned char>(bytes[0]) == 0xEFu
            && static_cast<unsigned char>(bytes[1]) == 0xBBu
            && static_cast<unsigned char>(bytes[2]) == 0xBFu)
        {
            bytes.erase(0, 3);
        }

        if (!isValidUtf8(bytes))
        {
            throw std::runtime_error{
                "File is not valid UTF-8 text."
            };
        }

        ExtractedProjectContent result;
        result.contentKind = "text/source";
        result.readerId = "utf8-text-v1";
        if (!bytes.empty())
        {
            result.segments.push_back(
                ProjectContentSegment{
                    .locator = "document",
                    .text = std::move(bytes)
                });
        }
        return result;
    }
}
