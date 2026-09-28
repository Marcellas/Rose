#pragma once

#include "tools/ToolTypes.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rose::agent
{
    inline constexpr std::size_t maximumRetainedSourceWindows{ 6 };
    inline constexpr std::size_t maximumRetainedEditedPaths{ 8 };


    // Bounded, ephemeral coding context for one Agent run.
    //
    // This is not Rose memory and it does not contain source text. It retains only
    // Rose-owned SourceWindowEvidence plus a compact list of source paths already
    // changed during the current request. The purpose is to let a small multi-file
    // coding workflow safely return to an earlier observed file without requiring
    // the model to echo the old preimage or losing provenance when another file is
    // read in between.
    struct CodingTaskWorkspaceState
    {
        std::vector<tools::SourceWindowEvidence> sourceWindows;
        std::vector<std::string> editedPaths;
    };


    // Retain one validated source window. Exact duplicate path/range entries are
    // replaced with the newest observation. Storage is bounded by
    // maximumRetainedSourceWindows and evicts the oldest window first.
    void observeCodingSourceWindow(
        CodingTaskWorkspaceState& state,
        const tools::SourceWindowEvidence& evidence);


    // Find the best retained Rose-owned preimage for one proposed
    // edit_text_file/replace_line_range request. The smallest covering window is
    // preferred; equally-sized windows prefer the newest observation.
    [[nodiscard]]
    std::optional<tools::SourceWindowEvidence> sourceWindowForRequest(
        const CodingTaskWorkspaceState& state,
        const tools::ToolRequest& request);


    // Update the workspace after a completed mutation.
    //
    // A successful text edit invalidates only retained windows for that exact file,
    // preserving evidence for other files in the same coding task. Path-shaping
    // operations clear the workspace because their effects can invalidate multiple
    // retained absolute paths. No failed mutation changes trusted workspace state.
    void observeCodingMutation(
        CodingTaskWorkspaceState& state,
        const tools::ToolRequest& request,
        const tools::ToolResult& result);


    // Compact Rose-owned metadata for the next control pass. Source contents and
    // hashes are intentionally omitted; the model must use the ordinary numbered
    // source observations for content and this metadata only for scope/provenance.
    [[nodiscard]]
    std::string formatCodingTaskWorkspaceMetadata(
        const CodingTaskWorkspaceState& state);

} // namespace rose::agent
