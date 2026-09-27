#pragma once

#include "integrations/IIntegrationPermissionStore.h"

#include <filesystem>

namespace rose::integrations
{
    class FileIntegrationPermissionStore final
        : public IIntegrationPermissionStore
    {
    public:
        explicit FileIntegrationPermissionStore(std::filesystem::path path);

        [[nodiscard]] IntegrationPermissionSnapshot load() override;
        void save(const IntegrationPermissionSnapshot& snapshot) override;

        [[nodiscard]] const std::filesystem::path& path() const noexcept;

    private:
        std::filesystem::path path_;
    };
}
