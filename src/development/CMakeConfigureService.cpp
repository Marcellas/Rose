#include "development/CMakeConfigureService.h"

#include "development/BoundedProcessRunner.h"
#include "development/ConfiguredCMakeProject.h"
#include "development/DiagnosticExtraction.h"

#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

namespace rose::development
{
    namespace
    {
        constexpr auto kConfigureTimeout = std::chrono::minutes{ 10 };
        constexpr std::size_t kMaximumCapturedBytes = 32U * 1024U;
        constexpr std::size_t kPreservedHeadBytes = 4U * 1024U;
    }


    CMakeConfigureResult LocalCMakeConfigureService::reconfigure(
        const CMakeConfigureRequest& request)
    {
        // This validates the pre-existing cache BEFORE invoking CMake. In
        // particular, Rose never turns a reconfigure request into authority to
        // create an arbitrary new build tree or switch generators/toolchains.
        const ConfiguredCMakeProject project =
            validateConfiguredCMakeProject(request.sourceDirectory);

        const std::filesystem::path cmakeExecutable =
            findExecutableOnPath(L"cmake.exe");

        std::vector<std::wstring> arguments;
        arguments.reserve(4U);
        arguments.push_back(L"-S");
        arguments.push_back(project.sourceDirectory.wstring());
        arguments.push_back(L"-B");
        arguments.push_back(project.buildDirectory.wstring());

        const BoundedProcessResult process =
            runBoundedProcess(
                BoundedProcessRequest{
                    .executable = cmakeExecutable,
                    .arguments = std::move(arguments),
                    .workingDirectory = project.sourceDirectory,
                    .timeout = kConfigureTimeout,
                    .maximumCapturedBytes = kMaximumCapturedBytes,
                    .preservedHeadBytes = kPreservedHeadBytes,
                    .operationName = "CMake reconfiguration"
                });

        return CMakeConfigureResult{
            .exitCode = process.exitCode,
            .timedOut = process.timedOut,
            .outputTruncated = process.outputTruncated,
            .sourceDirectory = project.sourceDirectory,
            .buildDirectory = project.buildDirectory,
            .cmakeExecutable = cmakeExecutable,
            .output = process.output,
            .diagnostics = extractSourceDiagnostics(
                process.output,
                project.sourceDirectory)
        };
    }

} // namespace rose::development
