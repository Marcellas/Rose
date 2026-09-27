#pragma once

#include "knowledge/ProjectContentReader.h"

#include <memory>

namespace rose::database { class IDatabaseService; }

namespace rose::knowledge
{
    class DatabaseProjectContentReader final : public IProjectContentReader
    {
    public:
        explicit DatabaseProjectContentReader(std::unique_ptr<database::IDatabaseService> databaseService);
        ~DatabaseProjectContentReader() override;

        [[nodiscard]] std::string_view id() const noexcept override { return "database-schema-v1"; }
        [[nodiscard]] std::uintmax_t maximumSourceBytes() const noexcept override { return 64ull * 1024ull * 1024ull * 1024ull; }
        [[nodiscard]] bool usesWholeFileByteBudget() const noexcept override { return false; }
        [[nodiscard]] bool supports(const std::filesystem::path& path) const noexcept override;
        [[nodiscard]] ExtractedProjectContent read(const std::filesystem::path& path, std::uintmax_t sourceBytes) const override;

    private:
        std::unique_ptr<database::IDatabaseService> databaseService_;
    };
}
