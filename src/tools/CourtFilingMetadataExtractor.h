#pragma once

#include "tools/ContextualOcrRepair.h"
#include "tools/DocumentRenameMetadata.h"

#include <string>
#include <string_view>
#include <vector>

namespace rose::tools
{
    struct CanonicalFilingTypeResult
    {
        bool ok{ false };
        std::string filingType;
        std::string reason;
    };

    struct DeterministicFilingExtraction
    {
        FilingMetadata metadata;
        std::vector<OcrRepair> repairs;
        std::string evidence;
        std::string reason;
    };

    // Turns a candidate caption/title into the compact filing type Rose is
    // allowed to place in a filename. This is deliberately stricter than a
    // spellchecker: party names, page footers, law-firm text, and prose clauses
    // are rejected or trimmed rather than silently becoming part of the title.
    [[nodiscard]]
    CanonicalFilingTypeResult canonicalizeCourtFilingType(
        std::string_view candidate);

    // Fast, deterministic first-pass extraction for common court filing stamps
    // and document captions. The local model remains the fallback for genuinely
    // ambiguous scans rather than being used for every easy document.
    [[nodiscard]]
    DeterministicFilingExtraction extractCourtFilingMetadata(
        std::string_view rawEvidence,
        bool requireFilingTime);

} // namespace rose::tools
