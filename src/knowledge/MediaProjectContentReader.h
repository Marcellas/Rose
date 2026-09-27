#pragma once

#include "knowledge/ProjectContentReader.h"

#include <memory>

namespace rose::media { class IMediaService; }

namespace rose::knowledge
{
    class MediaProjectContentReader final : public IProjectContentReader
    {
    public:
        explicit MediaProjectContentReader(std::unique_ptr<media::IMediaService> mediaService);
        ~MediaProjectContentReader() override;

        [[nodiscard]] std::string_view id() const noexcept override { return "media-metadata-v1"; }
        [[nodiscard]] std::uintmax_t maximumSourceBytes() const noexcept override { return 8ull * 1024ull * 1024ull * 1024ull; }
        [[nodiscard]] bool usesWholeFileByteBudget() const noexcept override { return false; }
        [[nodiscard]] bool supports(const std::filesystem::path& path) const noexcept override;
        [[nodiscard]] ExtractedProjectContent read(
            const std::filesystem::path& path,
            std::uintmax_t sourceBytes) const override;

    private:
        std::unique_ptr<media::IMediaService> mediaService_;
    };
}
