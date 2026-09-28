#pragma once

#include "tools/ToolTypes.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace rose::agent
{
    // Rose-owned reference to one grounded configure/build/test diagnostic. The message body
    // remains in ordinary untrusted tool output; this structure keeps only the
    // bounded location/classification fields that DiagnosticExtraction validated.
    struct SourceRepairDiagnostic
    {
        std::string producerTool;
        std::string path;
        std::size_t line{ 0 };
        std::size_t column{ 0 };
        std::string severity;
        std::string code;
    };


    // Ephemeral explanation of one provenance-bound line patch before mutation.
    //
    // The exact ToolRequest remains the authoritative action. This plan exists only
    // to make that action reviewable: what file/lines will change, which observed
    // preimage protects the write, and (when available) which grounded diagnostic
    // motivated the repair.
    struct SourceRepairPlan
    {
        std::string path;
        std::size_t startLine{ 0 };
        std::size_t lineCount{ 0 };
        std::string expectedDigest;
        std::string replacementProtocolText;
        std::optional<SourceRepairDiagnostic> diagnostic;
    };


    [[nodiscard]]
    bool isSourceDiagnosticMetadata(
        std::string_view trustedMetadata) noexcept;


    [[nodiscard]]
    bool sourceWindowCoversDiagnostic(
        const std::optional<tools::SourceWindowEvidence>& sourceWindow,
        std::string_view diagnosticMetadata);


    [[nodiscard]]
    std::optional<SourceRepairPlan> buildSourceRepairPlan(
        const tools::ToolRequest& effectiveRequest,
        const std::optional<tools::SourceWindowEvidence>& sourceWindow,
        std::string_view diagnosticMetadata);


    // Compact human-readable plan inserted into the confirmation surface. This is
    // bounded presentation only; /confirm still executes the exact stored request.
    [[nodiscard]]
    std::string formatSourceRepairPlan(
        const SourceRepairPlan& plan);

} // namespace rose::agent
