#pragma once

#include <filesystem>
#include <string_view>

namespace rose::development
{
    struct ConfiguredCMakeProject
    {
        std::filesystem::path sourceDirectory;
        std::filesystem::path buildDirectory;
        std::filesystem::path cacheFile;
    };

    [[nodiscard]]
    bool supportedCMakeConfiguration(
        std::string_view value) noexcept;

    // Validate one exact source tree against Rose's intentionally narrow CMake
    // convention: the configured build tree must be <source>/build and its
    // CMakeCache.txt must identify the same source directory.
    [[nodiscard]]
    ConfiguredCMakeProject validateConfiguredCMakeProject(
        const std::filesystem::path& requestedSource);
}
