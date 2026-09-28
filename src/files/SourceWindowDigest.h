#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rose::files
{
    // Convert the byte range returned by ReadFileTool::readTextFileLines into the
    // same logical source representation used by replace_line_range:
    //   - CRLF becomes LF
    //   - the delimiter following the final selected line is removed
    //
    // The returned text is still untrusted source content. This helper only gives
    // the read and mutation paths one deterministic representation to hash.
    [[nodiscard]]
    std::string canonicalObservedSourceWindow(
        std::string_view observedRangeText);

    // Hash each logical source line independently. Empty lines are preserved as
    // real entries. Rose stores these digests (not source bytes) in ephemeral
    // SourceWindowEvidence so a later patch may safely bind to any contiguous
    // subrange of the exact source window that was just observed.
    [[nodiscard]]
    std::vector<std::string> sourceLineSha256s(
        std::string_view logicalSourceWindow);

    // Produce one compact provenance digest from an ordered list of line hashes.
    // Because each line hash is fixed-width SHA-256 hex, this can be recomputed
    // for a contiguous subrange without retaining raw source text in trusted state.
    [[nodiscard]]
    std::string sourceWindowSha256FromLineDigests(
        std::span<const std::string> lineDigests);

    [[nodiscard]]
    std::string sourceWindowSha256(
        std::string_view logicalSourceWindow);

    [[nodiscard]]
    bool isSourceWindowSha256(
        std::string_view digest) noexcept;

} // namespace rose::files
