#pragma once

#include "knowledge/ProjectContentReader.h"
#include "tools/PdfTextExtractor.h"

#include <memory>

namespace rose::ocr
{
    class IOcrEngine;
}

namespace rose::knowledge
{
    class PdfProjectContentReader final : public IProjectContentReader
    {
    public:
        explicit PdfProjectContentReader(
            std::unique_ptr<ocr::IOcrEngine> ocrEngine);

        ~PdfProjectContentReader() override;

        [[nodiscard]] std::string_view id() const noexcept override
        { return "pdf-pdfium-ocr-v1"; }

        [[nodiscard]] std::uintmax_t maximumSourceBytes() const noexcept override
        { return 32u * 1024u * 1024u; }

        [[nodiscard]]
        bool supports(const std::filesystem::path& path) const noexcept override;

        [[nodiscard]]
        ExtractedProjectContent read(
            const std::filesystem::path& path,
            std::uintmax_t sourceBytes) const override;

    private:
        std::unique_ptr<ocr::IOcrEngine> ocrEngine_;
        tools::PdfTextExtractor extractor_;
    };
}
