#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "development/DiagnosticExtraction.h"

namespace rose::development
{
    struct CMakeBuildRequest
    {
        std::filesystem::path sourceDirectory;
        std::string configuration{ "Debug" };
        std::string target;
        std::size_t parallelJobs{ 8 };
    };

    struct CMakeBuildResult
    {
        std::uint32_t exitCode{ 0 };
        bool timedOut{ false };
        bool outputTruncated{ false };
        std::filesystem::path sourceDirectory;
        std::filesystem::path buildDirectory;
        std::filesystem::path cmakeExecutable;
        std::string output;
        std::vector<SourceDiagnostic> diagnostics;
    };

    class ICMakeBuildService
    {
    public:
        virtual ~ICMakeBuildService() = default;

        [[nodiscard]]
        virtual CMakeBuildResult build(
            const CMakeBuildRequest& request) = 0;
    };

    // Runs one already-configured CMake build without invoking a command shell.
    //
    // Security boundary:
    // - sourceDirectory must be absolute and contain CMakeLists.txt.
    // - the build tree is fixed to <sourceDirectory>/build.
    // - CMakeCache.txt must exist and point back to sourceDirectory.
    // - configuration/target/jobs are tightly validated.
    // - output and wall-clock runtime are bounded.
    //
    // A CMake build can execute project-defined custom commands, so callers must
    // still classify this service behind an externally consequential tool and
    // require explicit confirmation.
    class LocalCMakeBuildService final : public ICMakeBuildService
    {
    public:
        [[nodiscard]]
        CMakeBuildResult build(
            const CMakeBuildRequest& request) override;
    };
}
