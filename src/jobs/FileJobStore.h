#pragma once

#include "jobs/IJobStore.h"

#include <filesystem>

namespace rose::jobs
{
    class FileJobStore final : public IJobStore
    {
    public:
        explicit FileJobStore(std::filesystem::path path);

        [[nodiscard]] JobSnapshot load() override;
        void save(const JobSnapshot& snapshot) override;

        [[nodiscard]] const std::filesystem::path& path() const noexcept;

    private:
        std::filesystem::path path_;
    };
}
