#include "core/BuildIdentity.h"

#include "RoseBuildIdentity.generated.h"

#include <sstream>

#ifndef ROSE_BUILD_CONFIGURATION
#define ROSE_BUILD_CONFIGURATION "unknown"
#endif

#ifndef ROSE_BUILD_COMPILER_ID
#define ROSE_BUILD_COMPILER_ID "unknown"
#endif

#ifndef ROSE_BUILD_COMPILER_VERSION
#define ROSE_BUILD_COMPILER_VERSION "unknown"
#endif

namespace rose::core
{

    BuildIdentity currentBuildIdentity() noexcept
    {
        return BuildIdentity{
            .version =
                ROSE_BUILD_PROJECT_VERSION,
            .revision =
                ROSE_BUILD_GIT_REVISION,
            .branch =
                ROSE_BUILD_GIT_BRANCH,
            .configuration =
                ROSE_BUILD_CONFIGURATION,
            .compilerId =
                ROSE_BUILD_COMPILER_ID,
            .compilerVersion =
                ROSE_BUILD_COMPILER_VERSION,
            .gitAvailable =
                ROSE_BUILD_GIT_AVAILABLE != 0,
            .sourceDirty =
                ROSE_BUILD_GIT_DIRTY != 0
        };
    }


    std::string formatBuildIdentity(
        const BuildIdentity& identity)
    {
        std::ostringstream stream;

        stream
            << "Rose v"
            << identity.version
            << " | "
            << identity.configuration
            << " | "
            << identity.compilerId;

        if (!identity.compilerVersion.empty())
        {
            stream
                << ' '
                << identity.compilerVersion;
        }

        stream
            << " | rev "
            << identity.revision;

        if (identity.sourceDirty)
        {
            stream
                << " (dirty)";
        }

        if (
            identity.gitAvailable
            && !identity.branch.empty()
            && identity.branch != "HEAD")
        {
            stream
                << " | branch "
                << identity.branch;
        }

        return stream.str();
    }

} // namespace rose::core
