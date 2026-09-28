#include "development/CMakeBuildService.h"

#include "development/BoundedProcessRunner.h"
#include "development/ConfiguredCMakeProject.h"
#include "development/DiagnosticExtraction.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rose::development
{
    namespace
    {
        constexpr auto kBuildTimeout = std::chrono::minutes{ 10 };
        constexpr std::size_t kMaximumCapturedBytes = 32U * 1024U;
        constexpr std::size_t kPreservedHeadBytes = 4U * 1024U;

        [[nodiscard]]
        std::string lowerAscii(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        [[nodiscard]]
        bool validTarget(const std::string_view value)
        {
            if (value.empty()) return true;
            if (value.size() > 128) return false;

            for (const char rawCharacter : value)
            {
                const unsigned char character =
                    static_cast<unsigned char>(rawCharacter);
                if (std::isalnum(character) != 0) continue;
                if (character == static_cast<unsigned char>('_')
                    || character == static_cast<unsigned char>('-')
                    || character == static_cast<unsigned char>('.')
                    || character == static_cast<unsigned char>('+')
                    || character == static_cast<unsigned char>(':'))
                {
                    continue;
                }
                return false;
            }

            const std::string lower = lowerAscii(std::string{ value });
            return lower != "install"
                && lower != "uninstall"
                && lower != "package"
                && lower != "package_source"
                && lower != "deploy";
        }

        [[nodiscard]]
        std::wstring widenAscii(const std::string_view value)
        {
            return std::wstring{ value.begin(), value.end() };
        }
    }

    CMakeBuildResult LocalCMakeBuildService::build(
        const CMakeBuildRequest& request)
    {
        if (!supportedCMakeConfiguration(request.configuration))
        {
            throw std::invalid_argument{
                "CMake configuration must be Debug, Release, RelWithDebInfo, or MinSizeRel."
            };
        }
        if (!validTarget(request.target))
        {
            throw std::invalid_argument{
                "CMake target is unsupported, potentially installation-like, or too long."
            };
        }
        if (request.parallelJobs == 0 || request.parallelJobs > 32)
            throw std::invalid_argument{ "CMake parallel job count must be from 1 to 32." };

        const ConfiguredCMakeProject project =
            validateConfiguredCMakeProject(request.sourceDirectory);

        const std::filesystem::path cmakeExecutable =
            findExecutableOnPath(L"cmake.exe");

        std::vector<std::wstring> arguments;
        arguments.reserve(request.target.empty() ? 6U : 8U);
        arguments.push_back(L"--build");
        arguments.push_back(project.buildDirectory.wstring());
        arguments.push_back(L"--config");
        arguments.push_back(widenAscii(request.configuration));
        if (!request.target.empty())
        {
            arguments.push_back(L"--target");
            arguments.push_back(widenAscii(request.target));
        }
        arguments.push_back(L"-j");
        arguments.push_back(std::to_wstring(request.parallelJobs));

        const BoundedProcessResult process =
            runBoundedProcess(
                BoundedProcessRequest{
                    .executable = cmakeExecutable,
                    .arguments = std::move(arguments),
                    .workingDirectory = project.sourceDirectory,
                    .timeout = kBuildTimeout,
                    .maximumCapturedBytes = kMaximumCapturedBytes,
                    .preservedHeadBytes = kPreservedHeadBytes,
                    .operationName = "CMake build"
                });

        return CMakeBuildResult{
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
}
