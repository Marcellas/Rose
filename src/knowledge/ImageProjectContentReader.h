#pragma once

#include "knowledge/ProjectContentReader.h"

#include <memory>

namespace rose::ocr
{
    class IOcrEngine;
}

namespace rose::knowledge
{
    // Bulk project indexing deliberately performs OCR only. Semantic vision is
    // available on demand through inspect_image so indexing a folder of photos does
    // not repeatedly load a multimodal model or consume large amounts of VRAM.
    class ImageProjectContentReader final : public IProjectContentReader
    {
    public:
        explicit ImageProjectContentReader(
            std::unique_ptr<ocr::IOcrEngine> ocrEngine);
        ~ImageProjectContentReader() override;

        [[nodiscard]] std::string_view id() const noexcept override
        { return "image-ocr-v1"; }
        [[nodiscard]] std::uintmax_t maximumSourceBytes() const noexcept override
        { return 24u * 1024u * 1024u; }

        [[nodiscard]] bool supports(const std::filesystem::path& path) const noexcept override;
        [[nodiscard]] ExtractedProjectContent read(
            const std::filesystem::path& path,
            std::uintmax_t sourceBytes) const override;

    private:
        std::unique_ptr<ocr::IOcrEngine> ocrEngine_;
    };
}
