#include "tools/ReadFileTool.h"

#include "permissions/PermissionSystem.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace rose::tools
{

    namespace
    {
        struct ValidatedFile
        {
            std::uintmax_t size{ 0 };
        };


        [[nodiscard]]
        ValidatedFile validateReadableRegularFile(
            const std::filesystem::path& path)
        {
            std::error_code error;

            if (!std::filesystem::exists(path, error) || error)
            {
                throw std::runtime_error{
                    "Attached file no longer exists: "
                    + path.string()
                };
            }

            if (!std::filesystem::is_regular_file(path, error) || error)
            {
                throw std::runtime_error{
                    "The attachment is not a regular file: "
                    + path.string()
                };
            }

            const std::uintmax_t size =
                std::filesystem::file_size(
                    path,
                    error);

            if (error)
            {
                throw std::runtime_error{
                    "Could not determine attachment size: "
                    + path.string()
                };
            }

            return ValidatedFile{
                .size = size
            };
        }


        [[nodiscard]]
        std::ifstream openBinaryStream(
            const std::filesystem::path& path)
        {
            std::ifstream stream{
                path,
                std::ios::binary
            };

            if (!stream)
            {
                throw std::runtime_error{
                    "Could not open attached file for reading: "
                    + path.string()
                };
            }

            return stream;
        }
    } // namespace


    ReadFileTool::ReadFileTool(
        permissions::PermissionSystem& permissions,
        const ReadFileConfig config)
        : permissions_{ permissions }
        , config_{ config }
    {
        if (config_.maximumTextBytes == 0)
        {
            throw std::invalid_argument{
                "ReadFileTool maximumTextBytes must be greater than zero."
            };
        }

        if (config_.maximumBinaryBytes == 0)
        {
            throw std::invalid_argument{
                "ReadFileTool maximumBinaryBytes must be greater than zero."
            };
        }
    }


    ReadTextFileResult ReadFileTool::readTextFile(
        const std::filesystem::path& path)
    {
        // Authorization is consumed before any file contents are opened.
        if (!permissions_.consumeReadOnce(path))
        {
            throw std::runtime_error{
                "ReadFileTool denied access because no one-shot read permission exists for that file."
            };
        }

        const ValidatedFile file =
            validateReadableRegularFile(path);

        const std::size_t bytesToRead =
            static_cast<std::size_t>(
                std::min<std::uintmax_t>(
                    file.size,
                    static_cast<std::uintmax_t>(
                        config_.maximumTextBytes)));

        std::ifstream stream =
            openBinaryStream(path);

        std::string bytes(
            bytesToRead,
            '\0');

        if (bytesToRead > 0)
        {
            stream.read(
                bytes.data(),
                static_cast<std::streamsize>(
                    bytesToRead));

            const std::streamsize actual =
                stream.gcount();

            if (actual < 0)
            {
                throw std::runtime_error{
                    "Could not read attached file: "
                    + path.string()
                };
            }

            bytes.resize(
                static_cast<std::size_t>(actual));
        }

        if (looksLikeBinary(bytes))
        {
            throw std::runtime_error{
                "This attachment appears to be binary. The current text reader supports UTF-8 text/source files only: "
                + path.filename().string()
            };
        }

        // Strip a UTF-8 BOM before validating/model injection.
        if (
            bytes.size() >= 3
            && static_cast<unsigned char>(bytes[0]) == 0xEFu
            && static_cast<unsigned char>(bytes[1]) == 0xBBu
            && static_cast<unsigned char>(bytes[2]) == 0xBFu)
        {
            bytes.erase(0, 3);
        }

        if (!isValidUtf8(bytes))
        {
            throw std::runtime_error{
                "This attachment is not valid UTF-8 text yet: "
                + path.filename().string()
            };
        }

        return ReadTextFileResult{
            .path = path,
            .displayName = path.filename().string(),
            .text = std::move(bytes),
            .originalSize = file.size,
            .truncated = file.size
                > static_cast<std::uintmax_t>(
                    config_.maximumTextBytes)
        };
    }


    ReadBinaryFileResult ReadFileTool::readBinaryFile(
        const std::filesystem::path& path)
    {
        // Binary parsing is still permission-gated by exactly the same one-shot
        // policy as text files.
        if (!permissions_.consumeReadOnce(path))
        {
            throw std::runtime_error{
                "ReadFileTool denied access because no one-shot read permission exists for that file."
            };
        }

        const ValidatedFile file =
            validateReadableRegularFile(path);

        if (
            file.size
            > static_cast<std::uintmax_t>(
                config_.maximumBinaryBytes))
        {
            throw std::runtime_error{
                "This attachment is too large for Rose's current binary-document limit ("
                + std::to_string(config_.maximumBinaryBytes)
                + " bytes): "
                + path.filename().string()
            };
        }

        if (
            file.size
            > static_cast<std::uintmax_t>(
                std::numeric_limits<std::size_t>::max()))
        {
            throw std::runtime_error{
                "This attachment is too large for this process to address safely: "
                + path.filename().string()
            };
        }

        std::ifstream stream =
            openBinaryStream(path);

        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(
                file.size));

        if (!bytes.empty())
        {
            stream.read(
                reinterpret_cast<char*>(
                    bytes.data()),
                static_cast<std::streamsize>(
                    bytes.size()));

            if (
                stream.gcount()
                != static_cast<std::streamsize>(
                    bytes.size()))
            {
                throw std::runtime_error{
                    "Could not read the complete attached file: "
                    + path.string()
                };
            }
        }

        return ReadBinaryFileResult{
            .path = path,
            .displayName = path.filename().string(),
            .bytes = std::move(bytes),
            .originalSize = file.size
        };
    }


    bool ReadFileTool::looksLikeBinary(
        const std::string& bytes) noexcept
    {
        // NUL is a strong binary/UTF-16 signal and should never be injected into
        // the current UTF-8 model path as plain source text.
        return bytes.find('\0') != std::string::npos;
    }


    bool ReadFileTool::isValidUtf8(
        const std::string& bytes) noexcept
    {
        const auto* data =
            reinterpret_cast<const unsigned char*>(
                bytes.data());

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
                    return false; // overlong two-byte encoding
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

} // namespace rose::tools
