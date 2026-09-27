#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rose::documents
{
    struct OpenXmlPackageEntry
    {
        std::string path;
        std::string xml;
    };

    struct OpenXmlTextSegment
    {
        std::string locator;
        std::string text;
    };

    struct ExtractedOpenXmlDocument
    {
        std::string contentKind;
        std::vector<OpenXmlTextSegment> segments;
    };

    struct OpenXmlDocumentExtractorConfig
    {
        std::size_t maximumPackageBytes{ 32u * 1024u * 1024u };
        std::size_t maximumExtractedXmlBytes{ 32u * 1024u * 1024u };
    };

    // Read-only Office Open XML extractor for .docx/.docm, .xlsx/.xlsm and
    // .pptx/.pptm. The Windows adapter uses the framework ZIP support already
    // present in PowerShell/.NET, but source bytes are first copied into a private
    // Rose temporary package so the helper never receives the user's original path.
    //
    // extractEntries() is intentionally public and platform-neutral: it contains
    // the actual Word/Excel/PowerPoint interpretation logic and is independently
    // testable without PowerShell or Microsoft Office being installed.
    class OpenXmlDocumentExtractor final
    {
    public:
        explicit OpenXmlDocumentExtractor(
            OpenXmlDocumentExtractorConfig config = {});

        [[nodiscard]]
        ExtractedOpenXmlDocument extract(
            std::span<const std::uint8_t> packageBytes,
            std::string_view sourceExtension) const;

        [[nodiscard]]
        static ExtractedOpenXmlDocument extractEntries(
            std::string_view sourceExtension,
            const std::vector<OpenXmlPackageEntry>& entries);

    private:
        OpenXmlDocumentExtractorConfig config_;
    };
}
