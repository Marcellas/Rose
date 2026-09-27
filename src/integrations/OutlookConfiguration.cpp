#include "integrations/OutlookConfiguration.h"

#include <fstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace rose::integrations
{
    namespace
    {
        constexpr std::string_view magic{ "ROSEOUTLOOK01" };

        void validateToken(std::string_view value, std::string_view field, std::size_t maximum)
        {
            if (value.empty() || value.size() > maximum)
                throw std::invalid_argument{ std::string{ field } + " is empty or too long." };
            for (const unsigned char c : value)
            {
                const bool allowed =
                    (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
                if (!allowed)
                    throw std::invalid_argument{ std::string{ field } + " contains unsupported characters." };
            }
        }
    }

    OutlookConfigurationStore::OutlookConfigurationStore(std::filesystem::path path)
        : path_{ std::move(path) }
    {
        if (path_.empty()) throw std::invalid_argument{ "OutlookConfigurationStore requires a path." };
    }

    OutlookConfiguration OutlookConfigurationStore::load() const
    {
        if (!std::filesystem::exists(path_)) return {};
        std::ifstream input{ path_, std::ios::binary };
        if (!input) throw std::runtime_error{ "Could not open Rose Outlook configuration." };

        std::string header;
        std::string clientId;
        std::string tenant;
        std::getline(input, header);
        std::getline(input, clientId);
        std::getline(input, tenant);
        if (!input || header != magic)
            throw std::runtime_error{ "Rose Outlook configuration has an invalid header." };
        validateToken(clientId, "Outlook client id", 128);
        validateToken(tenant, "Outlook tenant", 128);
        return OutlookConfiguration{ std::move(clientId), std::move(tenant) };
    }

    void OutlookConfigurationStore::save(const OutlookConfiguration& configuration) const
    {
        validateToken(configuration.clientId, "Outlook client id", 128);
        validateToken(configuration.tenant, "Outlook tenant", 128);
        const auto parent = path_.parent_path();
        if (!parent.empty())
        {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) throw std::runtime_error{ "Could not create Outlook configuration directory: " + error.message() };
        }
        std::filesystem::path temporary = path_;
        temporary += ".tmp";
        {
            std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
            if (!output) throw std::runtime_error{ "Could not create Rose Outlook configuration." };
            output << magic << '\n' << configuration.clientId << '\n' << configuration.tenant << '\n';
            if (!output) throw std::runtime_error{ "Could not write Rose Outlook configuration." };
        }
        std::error_code error;
        std::filesystem::remove(path_, error);
        error.clear();
        std::filesystem::rename(temporary, path_, error);
        if (error) throw std::runtime_error{ "Could not activate Rose Outlook configuration: " + error.message() };
    }
}
