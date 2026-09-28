#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "development/DiagnosticExtraction.h"

namespace rose::development
{
    struct CMakeConfigureRequest
    {
        std::filesystem::path sourceDirectory;
    };

    struct CMakeConfigureResult
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

    class ICMakeConfigureService
    {
    public:
        virtual ~ICMakeConfigureService() = default;

        [[nodiscard]]
        virtual CMakeConfigureResult reconfigure(
            const CMakeConfigureRequest& request) = 0;
    };

    // Re-runs CMake generation for one EXISTING, source-matched build tree.
    //
    // This service intentionally cannot create a new build tree, select a new
    // generator, inject -D cache variables, choose a toolchain file, use a preset,
    // or request installation/package behavior. The current <source>/build cache
    // must already identify the same source tree before cmake.exe is started.
    //
    // CMake configure/generate logic may execute project-defined scripts and
    // dependency discovery/download logic, so the Agent-facing tool remains an
    // ExternalEffect capability and always requires explicit confirmation.
    class LocalCMakeConfigureService final : public ICMakeConfigureService
    {
    public:
        [[nodiscard]]
        CMakeConfigureResult reconfigure(
            const CMakeConfigureRequest& request) override;
    };

} // namespace rose::development
