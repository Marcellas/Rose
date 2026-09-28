#include "development/ConfiguredCMakeProject.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace rose::development
{
    bool supportedCMakeConfiguration(
        const std::string_view value) noexcept
    {
        return value == "Debug"
            || value == "Release"
            || value == "RelWithDebInfo"
            || value == "MinSizeRel";
    }

    ConfiguredCMakeProject validateConfiguredCMakeProject(
        const std::filesystem::path& requestedSource)
    {
        if (!requestedSource.is_absolute())
            throw std::invalid_argument{ "CMake operations require an absolute source directory." };

        std::error_code error;
        const std::filesystem::path source =
            std::filesystem::weakly_canonical(requestedSource, error);
        if (error || !std::filesystem::is_directory(source, error) || error)
        {
            throw std::runtime_error{
                "CMake source directory does not exist or is not readable: "
                + requestedSource.string()
            };
        }

        if (!std::filesystem::is_regular_file(source / "CMakeLists.txt", error) || error)
        {
            throw std::invalid_argument{
                "CMake source directory does not contain CMakeLists.txt: "
                + source.string()
            };
        }

        const std::filesystem::path build = source / "build";
        if (!std::filesystem::is_directory(build, error) || error)
        {
            throw std::runtime_error{
                "Configured build directory does not exist: " + build.string()
            };
        }

        const std::filesystem::path cache = build / "CMakeCache.txt";
        if (!std::filesystem::is_regular_file(cache, error) || error)
        {
            throw std::runtime_error{
                "CMake build directory is not configured (missing CMakeCache.txt): "
                + build.string()
            };
        }

        std::ifstream input{ cache };
        if (!input)
            throw std::runtime_error{ "Could not read CMakeCache.txt: " + cache.string() };

        std::string homeDirectory;
        std::string line;
        constexpr std::string_view prefix{ "CMAKE_HOME_DIRECTORY:INTERNAL=" };
        while (std::getline(input, line))
        {
            if (line.starts_with(prefix))
            {
                homeDirectory = line.substr(prefix.size());
                break;
            }
        }

        if (homeDirectory.empty())
            throw std::runtime_error{ "CMakeCache.txt does not identify CMAKE_HOME_DIRECTORY." };

        const std::filesystem::path cachedSource{ homeDirectory };
        error.clear();
        const bool sameSource = std::filesystem::equivalent(source, cachedSource, error);
        if (error || !sameSource)
            throw std::runtime_error{ "Configured build tree belongs to a different source directory." };

        return ConfiguredCMakeProject{
            .sourceDirectory = source,
            .buildDirectory = build,
            .cacheFile = cache
        };
    }
}
