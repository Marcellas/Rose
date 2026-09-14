#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rose::permissions
{
    class PermissionSystem;
}

namespace rose::tools
{

    struct ReadFileConfig
    {
        // Text/source files are injected directly into a model request, so keep
        // this deliberately small. Larger documents should use chunking/retrieval.
        std::size_t maximumTextBytes{ 64u * 1024u };

        // Binary containers such as PDFs need their complete byte stream in order
        // to parse reliably. Reject oversized files instead of truncating them.
        std::size_t maximumBinaryBytes{ 64u * 1024u * 1024u };
    };


    struct ReadTextFileResult
    {
        std::filesystem::path path;
        std::string displayName;
        std::string text;
        std::uintmax_t originalSize{ 0 };
        bool truncated{ false };
    };


    struct ReadBinaryFileResult
    {
        std::filesystem::path path;
        std::string displayName;
        std::vector<std::uint8_t> bytes;
        std::uintmax_t originalSize{ 0 };
    };


    // Safe exact-file read boundary.
    //
    // ReadFileTool does not decide which file Rose is allowed to access. It
    // consumes a one-shot grant from PermissionSystem before opening anything.
    //
    // Text and binary reads are separate because their safety policies differ:
    //
    //     text   -> may be truncated to protect model context
    //     binary -> must be complete or rejected, because parsers need a whole file
    class ReadFileTool final
    {
    public:
        ReadFileTool(
            permissions::PermissionSystem& permissions,
            ReadFileConfig config = {});

        [[nodiscard]]
        ReadTextFileResult readTextFile(
            const std::filesystem::path& path);

        [[nodiscard]]
        ReadBinaryFileResult readBinaryFile(
            const std::filesystem::path& path);

    private:
        [[nodiscard]]
        static bool looksLikeBinary(
            const std::string& bytes) noexcept;

        [[nodiscard]]
        static bool isValidUtf8(
            const std::string& bytes) noexcept;

        permissions::PermissionSystem& permissions_;
        ReadFileConfig config_;
    };

} // namespace rose::tools
