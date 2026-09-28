#include "files/TextFileMutationService.h"

#include "files/FileFormatCatalog.h"
#include "files/SourceWindowDigest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace rose::files
{
    namespace
    {
        constexpr std::size_t maximumFileBytes{ 4u * 1024u * 1024u };
        constexpr std::size_t maximumMutationTextBytes{ 1u * 1024u * 1024u };
        constexpr std::size_t maximumLinePatchBytes{ 64u * 1024u };
        constexpr std::size_t maximumLinePatchLines{ 200u };

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
                    if (codePoint == 0)
                    {
                        return false;
                    }
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

                if (index + continuationCount >= bytes.size())
                {
                    return false;
                }

                for (std::size_t offset = 1; offset <= continuationCount; ++offset)
                {
                    const unsigned char continuation = data[index + offset];
                    if ((continuation & 0xC0u) != 0x80u)
                    {
                        return false;
                    }

                    codePoint =
                        (codePoint << 6u)
                        | (continuation & 0x3Fu);
                }

                if (
                    (continuationCount == 1 && codePoint < 0x80u)
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

        void validateMutationText(
            const std::string_view text,
            const std::string_view fieldName)
        {
            if (text.size() > maximumMutationTextBytes)
            {
                throw std::invalid_argument{
                    std::string{ fieldName }
                    + " exceeds Rose's 1 MiB text mutation payload bound."
                };
            }

            if (text.find('\0') != std::string_view::npos || !isValidUtf8(text))
            {
                throw std::invalid_argument{
                    std::string{ fieldName }
                    + " must be valid UTF-8 text and may not contain NUL bytes."
                };
            }
        }

        [[nodiscard]]
        std::filesystem::path validateTargetPath(
            const std::filesystem::path& rawPath)
        {
            if (!rawPath.is_absolute())
            {
                throw std::invalid_argument{
                    "Text file editing requires an absolute file path."
                };
            }

            const std::filesystem::path path = rawPath.lexically_normal();

            if (!isTextSourceFile(path))
            {
                throw std::invalid_argument{
                    "Text file editing only accepts recognized text/source document formats."
                };
            }

            std::error_code error;
            const std::filesystem::file_status linkStatus =
                std::filesystem::symlink_status(path, error);

            if (error)
            {
                throw std::system_error{
                    error,
                    "Could not inspect text mutation target"
                };
            }

            if (std::filesystem::is_symlink(linkStatus))
            {
                throw std::runtime_error{
                    "Text file editing does not follow symbolic-link targets in this checkpoint: "
                    + path.string()
                };
            }

            if (!std::filesystem::is_regular_file(linkStatus))
            {
                throw std::runtime_error{
                    "Text file to edit does not exist as a regular file: "
                    + path.string()
                };
            }

            return path;
        }

        [[nodiscard]]
        std::string readWholeTextFile(
            const std::filesystem::path& path)
        {
            std::error_code error;
            const std::uintmax_t sourceBytes =
                std::filesystem::file_size(path, error);

            if (error)
            {
                throw std::system_error{
                    error,
                    "Could not determine text mutation target size"
                };
            }

            if (sourceBytes > static_cast<std::uintmax_t>(maximumFileBytes))
            {
                throw std::runtime_error{
                    "Text file exceeds Rose's 4 MiB mutation bound: "
                    + path.string()
                };
            }

            if (sourceBytes
                > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)()))
            {
                throw std::runtime_error{
                    "Text file is too large for this process to address safely: "
                    + path.string()
                };
            }

            std::ifstream input{ path, std::ios::binary };
            if (!input)
            {
                throw std::runtime_error{
                    "Could not open text file for mutation: "
                    + path.string()
                };
            }

            std::string bytes(
                static_cast<std::size_t>(sourceBytes),
                '\0');

            if (!bytes.empty())
            {
                input.read(
                    bytes.data(),
                    static_cast<std::streamsize>(bytes.size()));

                if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
                {
                    throw std::runtime_error{
                        "Could not read the complete text mutation target: "
                        + path.string()
                    };
                }
            }

            if (bytes.find('\0') != std::string::npos)
            {
                throw std::runtime_error{
                    "Text mutation target appears to be binary or UTF-16; Rose edits UTF-8 text only: "
                    + path.filename().string()
                };
            }

            if (!isValidUtf8(bytes))
            {
                throw std::runtime_error{
                    "Text mutation target is not valid UTF-8: "
                    + path.filename().string()
                };
            }

            return bytes;
        }

        [[nodiscard]]
        std::size_t uniqueOccurrenceOffset(
            const std::string_view source,
            const std::string_view needle)
        {
            if (needle.empty())
            {
                throw std::invalid_argument{
                    "replace_text/remove_text requires non-empty find_text."
                };
            }

            const std::size_t first = source.find(needle);
            if (first == std::string_view::npos)
            {
                throw std::runtime_error{
                    "Text mutation target did not contain the requested exact find_text."
                };
            }

            // Search from first + 1 instead of first + needle.size() so overlapping
            // occurrences are also considered ambiguous (for example aa in aaa).
            if (source.find(needle, first + 1) != std::string_view::npos)
            {
                throw std::runtime_error{
                    "Text mutation find_text is ambiguous because it occurs more than once. "
                    "Provide a larger unique exact match."
                };
            }

            return first;
        }

        struct LineContentRange
        {
            std::size_t begin{};
            std::size_t end{};
            std::string logicalText{};
        };

        [[nodiscard]]
        std::string normalizeLineEndingsToLf(
            const std::string_view text)
        {
            std::string normalized;
            normalized.reserve(text.size());

            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (
                    text[index] == '\r'
                    && index + 1 < text.size()
                    && text[index + 1] == '\n')
                {
                    normalized.push_back('\n');
                    ++index;
                    continue;
                }

                normalized.push_back(text[index]);
            }

            return normalized;
        }

        [[nodiscard]]
        std::string preferredLineEnding(
            const std::string_view source)
        {
            const std::size_t newline = source.find('\n');
            if (
                newline != std::string_view::npos
                && newline > 0
                && source[newline - 1] == '\r')
            {
                return "\r\n";
            }

            return "\n";
        }

        [[nodiscard]]
        std::string encodeLogicalLineEndings(
            const std::string_view logicalText,
            const std::string_view lineEnding)
        {
            const std::string normalized =
                normalizeLineEndingsToLf(logicalText);

            if (lineEnding == "\n")
            {
                return normalized;
            }

            std::string encoded;
            encoded.reserve(
                normalized.size()
                + static_cast<std::size_t>(std::count(
                    normalized.begin(), normalized.end(), '\n')));

            for (const char character : normalized)
            {
                if (character == '\n')
                {
                    encoded.append(lineEnding);
                }
                else
                {
                    encoded.push_back(character);
                }
            }

            return encoded;
        }

        [[nodiscard]]
        std::size_t logicalLineCount(
            const std::string_view logicalText) noexcept
        {
            return static_cast<std::size_t>(std::count(
                logicalText.begin(), logicalText.end(), '\n')) + 1u;
        }

        [[nodiscard]]
        LineContentRange locateLineContentRange(
            const std::string_view source,
            const std::size_t startLine,
            const std::size_t lineCount)
        {
            if (startLine == 0 || lineCount == 0)
            {
                throw std::invalid_argument{
                    "replace_line_range requires positive one-based start_line and line_count."
                };
            }

            if (lineCount > maximumLinePatchLines)
            {
                throw std::invalid_argument{
                    "replace_line_range line_count exceeds Rose's 200-line patch bound."
                };
            }

            std::size_t cursor{ 0 };
            if (
                source.size() >= 3
                && static_cast<unsigned char>(source[0]) == 0xEFu
                && static_cast<unsigned char>(source[1]) == 0xBBu
                && static_cast<unsigned char>(source[2]) == 0xBFu)
            {
                cursor = 3;
            }

            if (cursor >= source.size())
            {
                throw std::runtime_error{
                    "replace_line_range start_line is beyond the end of the text file."
                };
            }

            for (std::size_t line = 1; line < startLine; ++line)
            {
                const std::size_t newline = source.find('\n', cursor);
                if (newline == std::string_view::npos)
                {
                    throw std::runtime_error{
                        "replace_line_range start_line is beyond the end of the text file."
                    };
                }

                cursor = newline + 1;
                if (cursor >= source.size())
                {
                    throw std::runtime_error{
                        "replace_line_range start_line is beyond the end of the text file."
                    };
                }
            }

            const std::size_t begin = cursor;
            std::size_t end = begin;

            for (std::size_t selected = 0; selected < lineCount; ++selected)
            {
                if (cursor >= source.size())
                {
                    throw std::runtime_error{
                        "replace_line_range line_count extends beyond the end of the text file."
                    };
                }

                const std::size_t newline = source.find('\n', cursor);
                const bool finalSelectedLine = selected + 1 == lineCount;

                if (!finalSelectedLine)
                {
                    if (newline == std::string_view::npos)
                    {
                        throw std::runtime_error{
                            "replace_line_range line_count extends beyond the end of the text file."
                        };
                    }

                    cursor = newline + 1;
                    continue;
                }

                if (newline == std::string_view::npos)
                {
                    end = source.size();
                }
                else
                {
                    end = newline;
                    if (end > cursor && source[end - 1] == '\r')
                    {
                        --end;
                    }
                }
            }

            return LineContentRange{
                .begin = begin,
                .end = end,
                .logicalText = normalizeLineEndingsToLf(
                    source.substr(begin, end - begin))
            };
        }

        void validateLinePatchText(
            const std::string_view text,
            const std::string_view fieldName)
        {
            validateMutationText(text, fieldName);

            if (text.size() > maximumLinePatchBytes)
            {
                throw std::invalid_argument{
                    std::string{ fieldName }
                    + " exceeds Rose's 64 KiB line-patch payload bound."
                };
            }
        }

        void validateOutputBound(
            const std::string_view output)
        {
            if (output.size() > maximumFileBytes)
            {
                throw std::runtime_error{
                    "Text mutation output would exceed Rose's 4 MiB file bound."
                };
            }
        }

        [[nodiscard]]
        std::filesystem::path siblingTemporaryPath(
            const std::filesystem::path& finalPath,
            const std::string_view label)
        {
            static std::atomic<std::uint64_t> sequence{ 0 };

            for (std::size_t attempt = 0; attempt < 32; ++attempt)
            {
                const auto stamp =
                    std::chrono::steady_clock::now().time_since_epoch().count();

                const std::filesystem::path candidate =
                    finalPath.parent_path()
                    / (finalPath.filename().string()
                       + ".rose-"
                       + std::string{ label }
                       + "-"
                       + std::to_string(stamp)
                       + "-"
                       + std::to_string(sequence.fetch_add(1)));

                std::error_code error;
                const bool exists = std::filesystem::exists(candidate, error);
                if (!error && !exists)
                {
                    return candidate;
                }
            }

            throw std::runtime_error{
                "Could not allocate a unique sibling path for text mutation."
            };
        }

        class RemoveOnExit final
        {
        public:
            explicit RemoveOnExit(
                std::filesystem::path path)
                : path_{ std::move(path) }
            {
            }

            ~RemoveOnExit()
            {
                if (!active_)
                {
                    return;
                }

                std::error_code error;
                std::filesystem::remove(path_, error);
            }

            void release() noexcept
            {
                active_ = false;
            }

        private:
            std::filesystem::path path_;
            bool active_{ true };
        };

#ifdef _WIN32
        class UniqueHandle final
        {
        public:
            explicit UniqueHandle(
                HANDLE handle) noexcept
                : handle_{ handle }
            {
            }

            ~UniqueHandle()
            {
                if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle_);
                }
            }

            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;

            [[nodiscard]]
            HANDLE get() const noexcept
            {
                return handle_;
            }

        private:
            HANDLE handle_{ INVALID_HANDLE_VALUE };
        };
#endif

        void writeNewSiblingFile(
            const std::filesystem::path& path,
            const std::string_view bytes)
        {
#ifdef _WIN32
            UniqueHandle handle{
                CreateFileW(
                    path.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr)
            };

            if (handle.get() == INVALID_HANDLE_VALUE)
            {
                throw std::system_error{
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "Windows could not create Rose's staged text mutation file"
                };
            }

            std::size_t offset{ 0 };
            while (offset < bytes.size())
            {
                const std::size_t remaining = bytes.size() - offset;
                const DWORD requestBytes = static_cast<DWORD>(
                    (std::min)(remaining, static_cast<std::size_t>(1024u * 1024u)));

                DWORD written{ 0 };
                if (!WriteFile(
                        handle.get(),
                        bytes.data() + offset,
                        requestBytes,
                        &written,
                        nullptr))
                {
                    throw std::system_error{
                        static_cast<int>(GetLastError()),
                        std::system_category(),
                        "Windows could not write Rose's staged text mutation file"
                    };
                }

                if (written == 0)
                {
                    throw std::runtime_error{
                        "Windows reported a zero-byte write while staging a text mutation."
                    };
                }

                offset += static_cast<std::size_t>(written);
            }

            if (!FlushFileBuffers(handle.get()))
            {
                throw std::system_error{
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "Windows could not flush Rose's staged text mutation file"
                };
            }
#else
            if (std::filesystem::exists(path))
            {
                throw std::runtime_error{
                    "Rose's staged text mutation path unexpectedly already exists."
                };
            }

            std::ofstream output{
                path,
                std::ios::binary | std::ios::out
            };

            if (!output)
            {
                throw std::runtime_error{
                    "Could not create Rose's staged text mutation file."
                };
            }

            output.write(
                bytes.data(),
                static_cast<std::streamsize>(bytes.size()));
            output.flush();

            if (!output)
            {
                throw std::runtime_error{
                    "Could not completely write Rose's staged text mutation file."
                };
            }
#endif
        }

        void replaceFileTransactionally(
            const std::filesystem::path& original,
            const std::filesystem::path& staged)
        {
            const std::filesystem::path backup =
                siblingTemporaryPath(original, "backup");
            RemoveOnExit backupCleanup{ backup };

#ifdef _WIN32
            if (!ReplaceFileW(
                    original.c_str(),
                    staged.c_str(),
                    backup.c_str(),
                    REPLACEFILE_WRITE_THROUGH,
                    nullptr,
                    nullptr))
            {
                const DWORD replaceError = GetLastError();

                // ReplaceFileW normally leaves the replaced file intact on failure.
                // If Windows did create a backup while losing the original name,
                // make one best-effort local rollback before surfacing the error.
                std::error_code inspectError;
                const bool originalExists =
                    std::filesystem::exists(original, inspectError);
                inspectError.clear();
                const bool backupExists =
                    std::filesystem::exists(backup, inspectError);

                if (!originalExists && backupExists)
                {
                    std::error_code rollbackError;
                    std::filesystem::rename(backup, original, rollbackError);
                    if (!rollbackError)
                    {
                        backupCleanup.release();
                    }
                }

                throw std::system_error{
                    static_cast<int>(replaceError),
                    std::system_category(),
                    "Windows could not transactionally replace the edited text file"
                };
            }

            std::error_code removeError;
            const bool backupRemoved =
                std::filesystem::remove(backup, removeError);

            // If explicit cleanup did not remove the backup, leave the RAII guard
            // active so it gets one final best-effort removal at scope exit.
            if (backupRemoved && !removeError)
            {
                backupCleanup.release();
            }
#else
            std::error_code error;
            std::filesystem::rename(original, backup, error);
            if (error)
            {
                throw std::runtime_error{
                    "Could not stage original text file for replacement: "
                    + error.message()
                };
            }

            error.clear();
            std::filesystem::rename(staged, original, error);
            if (error)
            {
                std::error_code rollbackError;
                std::filesystem::rename(backup, original, rollbackError);

                if (rollbackError)
                {
                    throw std::runtime_error{
                        "Text mutation replacement failed and rollback also failed. replacement="
                        + error.message()
                        + ", rollback="
                        + rollbackError.message()
                    };
                }

                backupCleanup.release();
                throw std::runtime_error{
                    "Could not replace text file after mutation: "
                    + error.message()
                };
            }

            error.clear();
            const bool backupRemoved =
                std::filesystem::remove(backup, error);

            // Keep the cleanup guard armed if the explicit removal failed.
            if (backupRemoved && !error)
            {
                backupCleanup.release();
            }
#endif
        }

        [[nodiscard]]
        std::string operationName(
            const TextFileMutationKind kind)
        {
            switch (kind)
            {
            case TextFileMutationKind::ReplaceExactText:
                return "replace_text";
            case TextFileMutationKind::AppendText:
                return "append_text";
            case TextFileMutationKind::RemoveText:
                return "remove_text";
            case TextFileMutationKind::ReplaceLineRange:
                return "replace_line_range";
            }

            return "unknown";
        }
    }

    TextFileMutationResult LocalTextFileMutationService::edit(
        const EditTextFileRequest& request)
    {
        const std::filesystem::path path =
            validateTargetPath(request.path);

        validateMutationText(request.findText, "find_text");
        validateMutationText(request.replacementText, "replacement_text");
        validateMutationText(request.text, "text");
        validateMutationText(request.expectedText, "expected_text");

        const std::string source =
            readWholeTextFile(path);

        std::string edited = source;
        std::size_t affectedCount{ 1 };

        switch (request.kind)
        {
        case TextFileMutationKind::ReplaceExactText:
        {
            const std::size_t offset =
                uniqueOccurrenceOffset(source, request.findText);

            edited.replace(
                offset,
                request.findText.size(),
                request.replacementText);
            break;
        }

        case TextFileMutationKind::AppendText:
            if (request.text.empty())
            {
                throw std::invalid_argument{
                    "append_text requires non-empty text."
                };
            }
            edited.append(request.text);
            break;

        case TextFileMutationKind::RemoveText:
        {
            const std::size_t offset =
                uniqueOccurrenceOffset(source, request.findText);

            edited.erase(
                offset,
                request.findText.size());
            break;
        }

        case TextFileMutationKind::ReplaceLineRange:
        {
            validateLinePatchText(request.replacementText, "replacement_text");

            const bool hasExpectedDigest =
                !request.expectedDigest.empty();

            if (hasExpectedDigest && !request.expectedText.empty())
            {
                throw std::invalid_argument{
                    "replace_line_range accepts expected_text or expected_digest, not both."
                };
            }

            const LineContentRange range =
                locateLineContentRange(
                    source,
                    request.startLine,
                    request.lineCount);

            if (hasExpectedDigest)
            {
                if (!isSourceWindowSha256(request.expectedDigest))
                {
                    throw std::invalid_argument{
                        "replace_line_range expected_digest must be a lowercase 64-character SHA-256 value."
                    };
                }

                const std::string currentDigest =
                    sourceWindowSha256(
                        range.logicalText);

                if (currentDigest != request.expectedDigest)
                {
                    throw std::runtime_error{
                        "replace_line_range source-window digest mismatch: the requested source lines no longer match the observed preimage. "
                        "Read the source window again before proposing another patch."
                    };
                }
            }
            else
            {
                validateLinePatchText(request.expectedText, "expected_text");

                const std::string expectedLogical =
                    normalizeLineEndingsToLf(request.expectedText);

                if (logicalLineCount(expectedLogical) != request.lineCount)
                {
                    throw std::invalid_argument{
                        "replace_line_range expected_text must describe exactly line_count logical lines."
                    };
                }

                if (range.logicalText != expectedLogical)
                {
                    throw std::runtime_error{
                        "replace_line_range preimage mismatch: the requested source lines no longer match expected_text. "
                        "Read the source window again before proposing another patch."
                    };
                }
            }

            const std::string replacementBytes =
                encodeLogicalLineEndings(
                    request.replacementText,
                    preferredLineEnding(source));

            if (
                logicalLineCount(normalizeLineEndingsToLf(request.replacementText))
                > maximumLinePatchLines)
            {
                throw std::invalid_argument{
                    "replace_line_range replacement_text exceeds Rose's 200-line patch bound."
                };
            }

            edited.replace(
                range.begin,
                range.end - range.begin,
                replacementBytes);

            affectedCount = request.lineCount;
            break;
        }
        }

        validateOutputBound(edited);

        const std::filesystem::path staged =
            siblingTemporaryPath(path, "edit");
        RemoveOnExit stagedCleanup{ staged };

        writeNewSiblingFile(staged, edited);

        // Do not silently clobber a concurrent editor. Re-read the bounded source
        // immediately before replacement and require byte-for-byte identity with
        // the version on which this mutation was computed.
        const std::string currentSource =
            readWholeTextFile(path);

        if (currentSource != source)
        {
            throw std::runtime_error{
                "Text file changed after Rose read it; mutation was cancelled without replacing the file."
            };
        }

        replaceFileTransactionally(path, staged);
        stagedCleanup.release();

        return TextFileMutationResult{
            .operation = operationName(request.kind),
            .detail = path.string(),
            .affectedCount = affectedCount
        };
    }
}
