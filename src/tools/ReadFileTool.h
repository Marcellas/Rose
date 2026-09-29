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

        // Diagnostic/source-window reads may need to reach well beyond the first
        // 64 KiB of a source file. Keep the scan bounded and symmetric with Rose's
        // current text-mutation maximum so a build error near the end of an ordinary
        // source file can still be inspected without loading arbitrarily large files.
        std::size_t maximumTextRangeScanBytes{ 4u * 1024u * 1024u };

        // Ordinary binary readers keep the historical 64 MiB default. Tools that
        // have reviewed semantics for larger containers may request a larger
        // per-call limit, but never above maximumBinarySafetyBytes.
        std::size_t maximumBinaryBytes{ 64u * 1024u * 1024u };

        // Hard ceiling for any one binary read. 512 MiB is a safety limit, not a
        // permanently reserved buffer: ReadFileTool allocates only the exact
        // accepted file size for the duration of that read.
        std::size_t maximumBinarySafetyBytes{ 512u * 1024u * 1024u };
    };


    struct ReadTextFileResult
    {
        std::filesystem::path path;
        std::string displayName;
        std::string text;
        std::uintmax_t originalSize{ 0 };
        bool truncated{ false };
    };


    struct ReadTextFileRangeResult
    {
        std::filesystem::path path;
        std::string displayName;
        std::string text;
        std::uintmax_t originalSize{ 0 };
        std::size_t requestedStartLine{ 1 };
        std::size_t requestedLineCount{ 0 };
        std::size_t returnedEndLine{ 0 };
        bool scanTruncated{ false };
        bool rangeTruncated{ false };
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
        ReadTextFileRangeResult readTextFileLines(
            const std::filesystem::path& path,
            std::size_t startLine,
            std::size_t lineCount);

        [[nodiscard]]
        ReadBinaryFileResult readBinaryFile(
            const std::filesystem::path& path);

        // Reviewed readers such as PDF may opt into a larger per-call limit. The
        // requested limit must remain within maximumBinarySafetyBytes.
        [[nodiscard]]
        ReadBinaryFileResult readBinaryFile(
            const std::filesystem::path& path,
            std::size_t maximumBytes);

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
