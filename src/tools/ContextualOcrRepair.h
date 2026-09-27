#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rose::tools
{
    // One conservative OCR correction. Raw extraction is never discarded; the
    // repair list is provenance that can be logged or persisted beside results.
    struct OcrRepair
    {
        std::string original;
        std::string replacement;
        std::string method;
        float confidence{ 0.0f };
    };

    struct OcrRepairResult
    {
        std::string text;
        std::vector<OcrRepair> repairs;
    };

    // Repair only high-confidence OCR damage using legal-document context and
    // common glyph confusions. This is intentionally NOT a general spellchecker:
    // names, docket codes, addresses, and unknown tokens are left alone unless a
    // strong structural/contextual rule supports the correction.
    [[nodiscard]]
    OcrRepairResult repairContextualOcr(
        std::string_view rawText);

} // namespace rose::tools
