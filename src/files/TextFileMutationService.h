#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace rose::files
{
    enum class TextFileMutationKind
    {
        ReplaceExactText,
        AppendText,
        RemoveText,
        ReplaceLineRange
    };

    struct EditTextFileRequest
    {
        std::filesystem::path path{};
        TextFileMutationKind kind{ TextFileMutationKind::ReplaceExactText };

        // ReplaceExactText and RemoveText use findText as the exact byte sequence to
        // locate. ReplaceExactText substitutes replacementText. AppendText appends text.
        // ReplaceLineRange uses one-based startLine/lineCount plus expectedText as a
        // logical-LF preimage. replacementText becomes the new logical line content.
        std::string findText{};
        std::string replacementText{};
        std::string text{};
        std::size_t startLine{};
        std::size_t lineCount{};
        std::string expectedText{};

        // Optional SHA-256 provenance from a prior exact source-window read.
        // replace_line_range accepts either expectedText or expectedDigest. The
        // digest lets Rose prove the current source still matches the window she
        // actually observed without copying a potentially large preimage back
        // through the control-model protocol.
        std::string expectedDigest{};
    };

    struct TextFileMutationResult
    {
        std::string operation;
        std::string detail;
        std::size_t affectedCount{};
    };

    class ITextFileMutationService
    {
    public:
        virtual ~ITextFileMutationService() = default;

        virtual TextFileMutationResult edit(
            const EditTextFileRequest& request) = 0;
    };

    // LocalTextFileMutationService performs bounded UTF-8 source/document edits.
    //
    // Ownership/lifetime:
    //   The service is stateless. A tool adapter may safely borrow one instance for
    //   the worker lifetime. Each edit owns its source/staged buffers locally.
    //
    // Safety/transaction model:
    //   - existing absolute regular file only
    //   - recognized plain-text/source families only
    //   - UTF-8 + NUL/binary rejection before mutation
    //   - 4 MiB whole-file and output bound; 1 MiB individual edit payload bound
    //   - replace/remove require exactly one matching occurrence
    //   - line-range replacement requires an exact observed preimage or source-window digest and <= 200 lines
    //   - writes a sibling temporary file first
    //   - verifies the source bytes did not change before replacement
    //   - replaces the original only after the staged file is fully flushed
    //
    // Windows uses ReplaceFileW with a short-lived sibling backup. The portable
    // fallback uses a rename/rollback sequence so tests can exercise the same
    // semantics without introducing a second file-format implementation.
    class LocalTextFileMutationService final : public ITextFileMutationService
    {
    public:
        TextFileMutationResult edit(
            const EditTextFileRequest& request) override;
    };
}
