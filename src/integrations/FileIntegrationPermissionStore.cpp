#include "integrations/FileIntegrationPermissionStore.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace rose::integrations
{
    namespace
    {
        constexpr std::array<char, 8> fileMagic{
            'R', 'O', 'S', 'E', 'I', 'P', '0', '1'
        };
        constexpr std::uint32_t formatVersion{ 1 };
        constexpr std::uint32_t maximumIntegrations{ 1024 };
        constexpr std::uint32_t maximumCapabilitiesPerIntegration{ 64 };
        constexpr std::uint32_t maximumStringBytes{ 1024 };

        template<typename T>
        void writeScalar(std::ostream& output, const T value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            output.write(reinterpret_cast<const char*>(&value), sizeof(T));
            if (!output)
            {
                throw std::runtime_error{ "Could not write Rose integration permissions." };
            }
        }

        template<typename T>
        T readScalar(std::istream& input)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            T value{};
            input.read(reinterpret_cast<char*>(&value), sizeof(T));
            if (!input)
            {
                throw std::runtime_error{ "Rose integration permission file is truncated." };
            }
            return value;
        }

        void writeString(std::ostream& output, const std::string_view value)
        {
            if (value.size() > maximumStringBytes)
            {
                throw std::runtime_error{ "Rose integration id is too large to persist." };
            }

            writeScalar(output, static_cast<std::uint32_t>(value.size()));
            if (!value.empty())
            {
                output.write(value.data(), static_cast<std::streamsize>(value.size()));
                if (!output)
                {
                    throw std::runtime_error{ "Could not write Rose integration id." };
                }
            }
        }

        std::string readString(std::istream& input)
        {
            const std::uint32_t size = readScalar<std::uint32_t>(input);
            if (size > maximumStringBytes)
            {
                throw std::runtime_error{ "Rose integration permission file contains an invalid id size." };
            }

            std::string value(size, '\0');
            if (size > 0)
            {
                input.read(value.data(), static_cast<std::streamsize>(size));
                if (!input)
                {
                    throw std::runtime_error{ "Rose integration permission file is truncated inside an id." };
                }
            }
            return value;
        }

        std::filesystem::path backupPathFor(const std::filesystem::path& destination)
        {
            std::filesystem::path backup = destination;
            backup += ".bak";
            return backup;
        }

        void ensureParentDirectory(const std::filesystem::path& path)
        {
            const std::filesystem::path parent = path.parent_path();
            if (parent.empty()) return;

            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error)
            {
                throw std::runtime_error{
                    "Could not create Rose integration settings directory: " + error.message()
                };
            }
        }

        void replaceFile(
            const std::filesystem::path& temporary,
            const std::filesystem::path& destination)
        {
            const std::filesystem::path backup = backupPathFor(destination);
            std::error_code error;
            const bool hadDestination = std::filesystem::exists(destination, error);
            if (error)
            {
                throw std::runtime_error{ "Could not inspect Rose integration permission file." };
            }

            if (hadDestination)
            {
                std::filesystem::remove(backup, error);
                error.clear();
                std::filesystem::rename(destination, backup, error);
                if (error)
                {
                    throw std::runtime_error{
                        "Could not stage Rose integration permission file: " + error.message()
                    };
                }
            }

            std::filesystem::rename(temporary, destination, error);
            if (error)
            {
                const std::string activationError = error.message();
                if (hadDestination)
                {
                    std::error_code rollbackError;
                    std::filesystem::rename(backup, destination, rollbackError);
                }
                throw std::runtime_error{
                    "Could not activate Rose integration permission file: " + activationError
                };
            }

            if (hadDestination)
            {
                std::filesystem::remove(backup, error);
            }
        }
    }

    FileIntegrationPermissionStore::FileIntegrationPermissionStore(
        std::filesystem::path path)
        : path_{ std::move(path) }
    {
        if (path_.empty())
        {
            throw std::invalid_argument{ "FileIntegrationPermissionStore requires a path." };
        }
    }

    IntegrationPermissionSnapshot FileIntegrationPermissionStore::load()
    {
        std::filesystem::path loadPath = path_;
        if (!std::filesystem::exists(loadPath))
        {
            const std::filesystem::path backup = backupPathFor(path_);
            if (!std::filesystem::exists(backup))
            {
                return {};
            }
            loadPath = backup;
        }

        std::ifstream input{ loadPath, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "Could not open Rose integration permission file: " + path_.string() };
        }

        std::array<char, fileMagic.size()> magic{};
        input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!input || magic != fileMagic)
        {
            throw std::runtime_error{ "Rose integration permission file has an invalid header." };
        }

        const std::uint32_t version = readScalar<std::uint32_t>(input);
        if (version != formatVersion)
        {
            throw std::runtime_error{ "Unsupported Rose integration permission format version." };
        }

        const std::uint32_t count = readScalar<std::uint32_t>(input);
        if (count > maximumIntegrations)
        {
            throw std::runtime_error{ "Rose integration permission file contains too many integrations." };
        }

        IntegrationPermissionSnapshot snapshot;
        snapshot.integrations.reserve(count);

        for (std::uint32_t index = 0; index < count; ++index)
        {
            IntegrationPermissionRecord record;
            record.integrationId = readString(input);
            record.updatedUnixMilliseconds = readScalar<std::int64_t>(input);

            const std::uint32_t capabilityCount = readScalar<std::uint32_t>(input);
            if (capabilityCount > maximumCapabilitiesPerIntegration)
            {
                throw std::runtime_error{ "Rose integration permission file contains too many capabilities." };
            }

            record.allowedCapabilities.reserve(capabilityCount);
            for (std::uint32_t capabilityIndex = 0;
                 capabilityIndex < capabilityCount;
                 ++capabilityIndex)
            {
                record.allowedCapabilities.push_back(
                    static_cast<IntegrationCapability>(readScalar<std::uint8_t>(input)));
            }

            snapshot.integrations.push_back(std::move(record));
        }

        return snapshot;
    }

    void FileIntegrationPermissionStore::save(
        const IntegrationPermissionSnapshot& snapshot)
    {
        if (snapshot.integrations.size() > maximumIntegrations)
        {
            throw std::runtime_error{ "Too many Rose integrations to persist." };
        }

        ensureParentDirectory(path_);
        std::filesystem::path temporary = path_;
        temporary += ".tmp";

        {
            std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
            if (!output)
            {
                throw std::runtime_error{ "Could not create temporary Rose integration permission file." };
            }

            output.write(fileMagic.data(), static_cast<std::streamsize>(fileMagic.size()));
            writeScalar(output, formatVersion);
            writeScalar(output, static_cast<std::uint32_t>(snapshot.integrations.size()));

            for (const IntegrationPermissionRecord& record : snapshot.integrations)
            {
                if (record.allowedCapabilities.size() > maximumCapabilitiesPerIntegration)
                {
                    throw std::runtime_error{ "Rose integration has too many capabilities to persist." };
                }

                writeString(output, record.integrationId);
                writeScalar(output, record.updatedUnixMilliseconds);
                writeScalar(output, static_cast<std::uint32_t>(record.allowedCapabilities.size()));
                for (const IntegrationCapability capability : record.allowedCapabilities)
                {
                    writeScalar(output, static_cast<std::uint8_t>(capability));
                }
            }

            output.flush();
            if (!output)
            {
                throw std::runtime_error{ "Could not flush Rose integration permission file." };
            }
        }

        replaceFile(temporary, path_);
    }

    const std::filesystem::path& FileIntegrationPermissionStore::path() const noexcept
    {
        return path_;
    }
}
