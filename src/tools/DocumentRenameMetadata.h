#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace rose::tools
{

    // High-confidence structured facts extracted from one court filing.
    //
    // Rose deliberately keeps these facts separate from the eventual filename.
    // The language model identifies evidence-backed metadata; deterministic C++
    // code owns formatting and Windows filename validation.
    enum class FilingMetadataStatus
    {
        Resolved,
        Ambiguous,
        Malformed
    };


    struct FilingMetadata
    {
        FilingMetadataStatus status{ FilingMetadataStatus::Malformed };

        // Canonical forms owned by Rose after parsing:
        //     filingDateIso  -> YYYY-MM-DD
        //     filingTimeHhmm -> HHMM, or empty when the filing time is not shown
        std::string filingDateIso;
        std::string filingTimeHhmm;
        std::string filingType;
        std::string confidence;
        std::string reason;
    };


    // Focused recovery result used after the combined metadata classifier could
    // not resolve a document.  Keeping date/time and filing type independent lets
    // Rose salvage one trustworthy field without forcing the model to solve every
    // field in one generation.
    struct FilingDateTimeMetadata
    {
        FilingMetadataStatus status{ FilingMetadataStatus::Malformed };
        std::string filingDateIso;
        std::string filingTimeHhmm;
        std::string confidence;
        std::string reason;
    };


    struct FilingTypeMetadata
    {
        FilingMetadataStatus status{ FilingMetadataStatus::Malformed };
        std::string filingType;
        std::string confidence;
        std::string reason;
    };


    struct FilingBasenameResult
    {
        bool ok{ false };
        std::string basename;
        std::string reason;
    };


    // Parse the compact structured classifier protocol.  The preferred form is:
    //
    //   OK|DATE=2026-09-15|TIME=2102|TYPE=Declaration of Petitioner|CONFIDENCE=high
    //
    // or:
    //
    //   AMBIGUOUS|MISSING=date,type|REASON=filing stamp is not visible
    //
    // A tolerant named-field multi-line fallback is accepted so small local
    // models are not rejected for harmless formatting differences.
    [[nodiscard]]
    FilingMetadata parseFilingMetadataProtocol(
        std::string_view raw);


    // Focused field parsers used by the ambiguity-recovery pass.
    //
    // Expected date/time protocol:
    //   OK|DATE=2026-09-15|TIME=2102|CONFIDENCE=high
    //
    // Expected type protocol:
    //   OK|TYPE=Declaration of Petitioner|CONFIDENCE=high
    [[nodiscard]]
    FilingDateTimeMetadata parseFilingDateTimeProtocol(
        std::string_view raw);

    [[nodiscard]]
    FilingTypeMetadata parseFilingTypeProtocol(
        std::string_view raw);


    // Construct Rose's canonical filing filename stem.  This function never asks
    // a model to choose punctuation or separators, which keeps a large rename
    // batch consistent even when classifier wording varies slightly.
    //
    // Canonical output:
    //     M.D.YYYY_HHMM Filing Type
    // or, when the filing time is genuinely unavailable and not required:
    //     M.D.YYYY Filing Type
    [[nodiscard]]
    FilingBasenameResult formatFilingBasename(
        const FilingMetadata& metadata,
        bool requireFilingTime,
        std::size_t maximumBytes);

} // namespace rose::tools
