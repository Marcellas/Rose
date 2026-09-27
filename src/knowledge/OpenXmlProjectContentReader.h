#pragma once

#include "documents/OpenXmlDocumentExtractor.h"
#include "knowledge/ProjectContentReader.h"

namespace rose::knowledge
{
    class OpenXmlProjectContentReader final : public IProjectContentReader
    {
    public:
        OpenXmlProjectContentReader() = default;

        [[nodiscard]] std::string_view id() const noexcept override
        { return "office-openxml-v1"; }
        [[nodiscard]] std::uintmax_t maximumSourceBytes() const noexcept override
        { return 32u * 1024u * 1024u; }

        [[nodiscard]] bool supports(const std::filesystem::path& path) const noexcept override;
        [[nodiscard]] ExtractedProjectContent read(
            const std::filesystem::path& path,
            std::uintmax_t sourceBytes) const override;

    private:
        documents::OpenXmlDocumentExtractor extractor_;
    };
}
