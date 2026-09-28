#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "development/DiagnosticExtraction.h"

namespace rose::development
{
    struct CMakeTestRequest
    {
        std::filesystem::path sourceDirectory;
        std::string configuration{ "Debug" };
        std::string testName;
        std::size_t parallelJobs{ 8 };
    };

    struct CMakeTestResult
    {
        std::uint32_t exitCode{ 0 };
        bool timedOut{ false };
        bool outputTruncated{ false };
        std::filesystem::path sourceDirectory;
        std::filesystem::path buildDirectory;
        std::filesystem::path ctestExecutable;
        std::string output;
        std::vector<SourceDiagnostic> diagnostics;
    };

    class ICMakeTestService
    {
    public:
        virtual ~ICMakeTestService() = default;

        [[nodiscard]]
        virtual CMakeTestResult run(
            const CMakeTestRequest& request) = 0;
    };

    // Runs tests already registered in one configured CMake build tree through
    // ctest.exe directly. It never launches an arbitrary user-provided executable
    // or command shell. Because CTest entries themselves can execute project code,
    // callers must keep this service behind an ExternalEffect confirmation gate.
    class LocalCMakeTestService final : public ICMakeTestService
    {
    public:
        [[nodiscard]]
        CMakeTestResult run(
            const CMakeTestRequest& request) override;
    };
}
