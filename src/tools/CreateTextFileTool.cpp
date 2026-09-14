#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef _WIN32
#include <Windows.h>
#endif

#include "tools/CreateTextFileTool.h"

#include "artifacts/Artifact.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace rose::tools
{
    namespace
    {
        constexpr std::size_t maximumContentBytes{
            1024u * 1024u
        };


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


        [[nodiscard]]
        std::string optionalArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

            if (found == request.arguments.end())
            {
                return {};
            }

            return found->second;
        }


        [[nodiscard]]
        std::string decodeTextEscapes(
            const std::string_view encoded)
        {
            std::string decoded;
            decoded.reserve(encoded.size());

            for (std::size_t index = 0; index < encoded.size(); ++index)
            {
                const char character = encoded[index];

                if (
                    character != '\\'
                    || index + 1 >= encoded.size())
                {
                    decoded.push_back(character);
                    continue;
                }

                const char next = encoded[index + 1];

                switch (next)
                {
                case 'n':
                    decoded.push_back('\n');
                    ++index;
                    break;

                case 'r':
                    decoded.push_back('\r');
                    ++index;
                    break;

                case 't':
                    decoded.push_back('\t');
                    ++index;
                    break;

                case '\\':
                    decoded.push_back('\\');
                    ++index;
                    break;

                default:
                    // Unknown escape: preserve it exactly rather than silently
                    // changing user content.
                    decoded.push_back('\\');
                    break;
                }
            }

            return decoded;
        }


        [[nodiscard]]
        std::string lowerAscii(
            std::string text)
        {
            std::transform(
                text.begin(),
                text.end(),
                text.begin(),
                [](const unsigned char value)
                {
                    if (value >= 'A' && value <= 'Z')
                    {
                        return static_cast<char>(
                            value - 'A' + 'a');
                    }

                    return static_cast<char>(value);
                });

            return text;
        }


        [[nodiscard]]
        bool isAllowedTextExtension(
            const std::filesystem::path& path)
        {
            static const std::unordered_set<std::string> allowed{
                ".txt",
                ".md",
                ".markdown",
                ".json",
                ".jsonl",
                ".yaml",
                ".yml",
                ".toml",
                ".ini",
                ".cfg",
                ".conf",
                ".csv",
                ".tsv",
                ".log",
                ".xml",
                ".html",
                ".htm",
                ".css",
                ".js",
                ".ts",
                ".py",
                ".c",
                ".cc",
                ".cpp",
                ".cxx",
                ".h",
                ".hh",
                ".hpp",
                ".hxx",
                ".cmake"
            };

            return allowed.contains(
                lowerAscii(
                    path.extension().string()));
        }


        [[nodiscard]]
        std::filesystem::path validateDestination(
            const std::string_view rawPath)
        {
            std::filesystem::path path{
                std::string{ rawPath }
            };

            if (!path.is_absolute())
            {
                throw std::invalid_argument{
                    "Create Text File requires an absolute destination path."
                };
            }

            path = path.lexically_normal();

            if (
                path.filename().empty()
                || !isAllowedTextExtension(path))
            {
                throw std::invalid_argument{
                    "Create Text File only accepts recognized text/code document extensions."
                };
            }

            const std::filesystem::path parent =
                path.parent_path();

            std::error_code error;

            if (
                parent.empty()
                || !std::filesystem::exists(parent, error)
                || error
                || !std::filesystem::is_directory(parent, error)
                || error)
            {
                throw std::runtime_error{
                    "Create Text File requires an existing parent directory: "
                    + parent.string()
                };
            }

            if (std::filesystem::exists(path, error))
            {
                throw std::runtime_error{
                    "Create Text File will not overwrite an existing file: "
                    + path.string()
                };
            }

            if (error)
            {
                throw std::system_error{
                    error,
                    "Could not inspect destination path"
                };
            }

            return path;
        }


#ifdef _WIN32
        class UniqueHandle final
        {
        public:
            explicit UniqueHandle(
                HANDLE value) noexcept
                : value_{ value }
            {
            }

            ~UniqueHandle()
            {
                if (
                    value_ != nullptr
                    && value_ != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(value_);
                }
            }

            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;

            [[nodiscard]]
            HANDLE get() const noexcept
            {
                return value_;
            }

        private:
            HANDLE value_{ INVALID_HANDLE_VALUE };
        };
#endif


        void createNewFile(
            const std::filesystem::path& path,
            const std::string_view content)
        {
#ifdef _WIN32
            const std::wstring widePath =
                path.wstring();

            UniqueHandle handle{
                CreateFileW(
                    widePath.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr)
            };

            if (handle.get() == INVALID_HANDLE_VALUE)
            {
                const DWORD error =
                    GetLastError();

                if (
                    error == ERROR_FILE_EXISTS
                    || error == ERROR_ALREADY_EXISTS)
                {
                    throw std::runtime_error{
                        "Create Text File will not overwrite an existing file: "
                        + path.string()
                    };
                }

                throw std::system_error{
                    static_cast<int>(error),
                    std::system_category(),
                    "Windows could not create the requested text file"
                };
            }

            std::size_t offset{ 0 };

            while (offset < content.size())
            {
                const std::size_t remaining =
                    content.size() - offset;

                const DWORD requestBytes =
                    static_cast<DWORD>(
                        (std::min)(
                            remaining,
                            static_cast<std::size_t>(
                                1024u * 1024u)));

                DWORD written{};

                if (!WriteFile(
                        handle.get(),
                        content.data() + offset,
                        requestBytes,
                        &written,
                        nullptr))
                {
                    throw std::system_error{
                        static_cast<int>(GetLastError()),
                        std::system_category(),
                        "Windows could not write the requested text file"
                    };
                }

                if (written == 0)
                {
                    throw std::runtime_error{
                        "Windows reported a zero-byte write while creating the text file."
                    };
                }

                offset +=
                    static_cast<std::size_t>(written);
            }

            if (!FlushFileBuffers(handle.get()))
            {
                throw std::system_error{
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "Windows could not flush the created text file"
                };
            }
#else
            // Portable fallback. Rose's primary target is Windows, where CREATE_NEW
            // supplies the stronger atomic no-overwrite guarantee above.
            if (std::filesystem::exists(path))
            {
                throw std::runtime_error{
                    "Create Text File will not overwrite an existing file: "
                    + path.string()
                };
            }

            std::ofstream output{
                path,
                std::ios::binary | std::ios::out
            };

            if (!output)
            {
                throw std::runtime_error{
                    "Could not create text file: "
                    + path.string()
                };
            }

            output.write(
                content.data(),
                static_cast<std::streamsize>(
                    content.size()));

            if (!output)
            {
                throw std::runtime_error{
                    "Could not write text file: "
                    + path.string()
                };
            }
#endif
        }
    }


    CreateTextFileTool::CreateTextFileTool()
        : descriptor_{
            .id = "create_text_file",
            .displayName = "Create Text File",
            .description =
                "Create one NEW UTF-8 text/code/document file at an explicit absolute "
                "path. The tool never overwrites an existing file and never creates "
                "missing parent directories. Use it only when the user explicitly asks "
                "Rose to create or write a new file and supplies the destination path.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "path",
                    .description =
                        "Absolute destination path including a text/code document extension.",
                    .type = ToolValueType::String,
                    .required = true
                },
                ToolParameterDescriptor{
                    .name = "content",
                    .description =
                        "UTF-8 file contents. Optional. Encode line breaks as literal \\n sequences.",
                    .type = ToolValueType::String,
                    .required = false
                }
            }
        }
    {
    }


    const ToolDescriptor& CreateTextFileTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult CreateTextFileTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "CreateTextFileTool received a request for a different tool."
            };
        }

        const std::filesystem::path destination =
            validateDestination(
                requiredArgument(
                    request,
                    "path"));

        const std::string content =
            decodeTextEscapes(
                optionalArgument(
                    request,
                    "content"));

        if (content.size() > maximumContentBytes)
        {
            throw std::invalid_argument{
                "Create Text File content exceeds the 1 MiB safety limit."
            };
        }

        createNewFile(
            destination,
            content);

        ToolResult result;
        result.success = true;
        result.message =
            "Created a new text file: "
            + destination.string();

        result.artifacts.push_back(
            artifacts::Artifact{
                .path = destination,
                .displayName = destination.filename().string(),
                .mediaType = "text/plain; charset=utf-8",
                .kind = artifacts::ArtifactKind::Document,
                .metadataPath = std::nullopt
            });

        return result;
    }

} // namespace rose::tools
