#pragma once

#include <filesystem>
#include <string>

namespace rose::integrations
{
    struct OutlookConfiguration
    {
        std::string clientId;
        std::string tenant;
    };

    class OutlookConfigurationStore final
    {
    public:
        explicit OutlookConfigurationStore(std::filesystem::path path);

        [[nodiscard]] OutlookConfiguration load() const;
        void save(const OutlookConfiguration& configuration) const;

    private:
        std::filesystem::path path_;
    };
}
