#pragma once

#include "knowledge/ProjectContentReader.h"

#include <memory>

namespace rose::shortcuts { class IShortcutService; }

namespace rose::knowledge
{
    class ShortcutProjectContentReader final : public IProjectContentReader
    {
    public:
        explicit ShortcutProjectContentReader(std::unique_ptr<shortcuts::IShortcutService> shortcutService);
        ~ShortcutProjectContentReader() override;

        [[nodiscard]] std::string_view id() const noexcept override { return "shortcut-metadata-v1"; }
        [[nodiscard]] std::uintmax_t maximumSourceBytes() const noexcept override { return 8ull * 1024ull * 1024ull; }
        [[nodiscard]] bool supports(const std::filesystem::path& path) const noexcept override;
        [[nodiscard]] ExtractedProjectContent read(const std::filesystem::path& path, std::uintmax_t sourceBytes) const override;

    private:
        std::unique_ptr<shortcuts::IShortcutService> shortcutService_;
    };
}
