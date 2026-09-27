#pragma once

#include <string>
#include <string_view>

namespace rose::core
{

    // Immutable metadata compiled into one Rose binary.
    //
    // The generated Git fields describe the source tree used for the build.
    // Configuration/compiler fields describe the actual target that produced
    // this executable.
    struct BuildIdentity
    {
        std::string_view version;
        std::string_view revision;
        std::string_view branch;
        std::string_view configuration;
        std::string_view compilerId;
        std::string_view compilerVersion;

        bool gitAvailable{ false };
        bool sourceDirty{ false };
    };


    [[nodiscard]]
    BuildIdentity currentBuildIdentity() noexcept;


    // Human-readable identity used by startup diagnostics and /build.
    [[nodiscard]]
    std::string formatBuildIdentity(
        const BuildIdentity& identity);

} // namespace rose::core
