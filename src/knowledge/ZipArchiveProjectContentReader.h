#pragma once

#include "archives/ZipArchiveService.h"
#include "knowledge/ProjectContentReader.h"

#include <cstddef>

namespace rose::knowledge
{
    struct ZipArchiveProjectContentReaderConfig
    {
        std::size_t maximumManifestBytes{ 96u * 1024u };
    };

    // Indexes ZIP metadata, not extracted member contents. This makes archives
    // discoverable by filename/member-name without silently unpacking arbitrary
    // files during a background Project Knowledge refresh.
    class ZipArchiveProjectContentReader final : public IProjectContentReader
    {
    public:
        explicit ZipArchiveProjectContentReader(
            ZipArchiveProjectContentReaderConfig config = {});

        [[nodiscard]]
        std::string_view id() const noexcept override;

        [[nodiscard]]
        std::uintmax_t maximumSourceBytes() const noexcept override;

        [[nodiscard]]
        bool usesWholeFileByteBudget() const noexcept override { return false; }

        [[nodiscard]]
        bool supports(
            const std::filesystem::path& path) const noexcept override;

        [[nodiscard]]
        ExtractedProjectContent read(
            const std::filesystem::path& path,
            std::uintmax_t sourceBytes) const override;

    private:
        ZipArchiveProjectContentReaderConfig config_;
        archives::WindowsZipArchiveService archiveService_;
    };
}
