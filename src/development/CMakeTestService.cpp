#include "development/CMakeTestService.h"

#include "development/BoundedProcessRunner.h"
#include "development/ConfiguredCMakeProject.h"
#include "development/DiagnosticExtraction.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace rose::development
{
    namespace
    {
        constexpr auto kTestRunTimeout = std::chrono::minutes{ 10 };
        constexpr std::size_t kMaximumCapturedBytes = 48U * 1024U;
        constexpr std::size_t kPreservedHeadBytes = 4U * 1024U;

        [[nodiscard]]
        bool validTestName(const std::string_view value)
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

            return true;
        }

        [[nodiscard]]
        std::string regexEscape(const std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size() * 2U);

            for (const char character : value)
            {
                switch (character)
                {
                case '.': case '^': case '$': case '|': case '(':
                case ')': case '[': case ']': case '*': case '+':
                case '?': case '{': case '}': case '\\':
                    escaped.push_back('\\');
                    break;
                default:
                    break;
                }
                escaped.push_back(character);
            }

            return escaped;
        }

        [[nodiscard]]
        std::wstring widenAscii(const std::string_view value)
        {
            return std::wstring{ value.begin(), value.end() };
        }
    }

    CMakeTestResult LocalCMakeTestService::run(
        const CMakeTestRequest& request)
    {
        if (!supportedCMakeConfiguration(request.configuration))
        {
            throw std::invalid_argument{
                "CTest configuration must be Debug, Release, RelWithDebInfo, or MinSizeRel."
            };
        }
        if (!validTestName(request.testName))
        {
            throw std::invalid_argument{
                "CTest exact test name contains unsupported characters or is too long."
            };
        }
        if (request.parallelJobs == 0 || request.parallelJobs > 32)
            throw std::invalid_argument{ "CTest parallel job count must be from 1 to 32." };

        const ConfiguredCMakeProject project =
            validateConfiguredCMakeProject(request.sourceDirectory);

        std::error_code error;
        const std::filesystem::path ctestFile =
            project.buildDirectory / "CTestTestfile.cmake";
        if (!std::filesystem::is_regular_file(ctestFile, error) || error)
        {
            throw std::runtime_error{
                "Configured build tree has no CTest registration. Re-run CMake configure after adding tests: "
                + project.buildDirectory.string()
            };
        }

        const std::filesystem::path ctestExecutable =
            findExecutableOnPath(L"ctest.exe");

        std::vector<std::wstring> arguments;
        arguments.reserve(request.testName.empty() ? 11U : 13U);
        arguments.push_back(L"--test-dir");
        arguments.push_back(project.buildDirectory.wstring());
        arguments.push_back(L"-C");
        arguments.push_back(widenAscii(request.configuration));
        arguments.push_back(L"--output-on-failure");
        arguments.push_back(L"--no-tests=error");
        arguments.push_back(L"--timeout");
        arguments.push_back(L"120");
        arguments.push_back(L"-j");
        arguments.push_back(std::to_wstring(request.parallelJobs));

        if (!request.testName.empty())
        {
            const std::string exactRegex =
                "^" + regexEscape(request.testName) + "$";
            arguments.push_back(L"-R");
            arguments.push_back(widenAscii(exactRegex));
        }

        const BoundedProcessResult process =
            runBoundedProcess(
                BoundedProcessRequest{
                    .executable = ctestExecutable,
                    .arguments = std::move(arguments),
                    .workingDirectory = project.sourceDirectory,
                    .timeout = kTestRunTimeout,
                    .maximumCapturedBytes = kMaximumCapturedBytes,
                    .preservedHeadBytes = kPreservedHeadBytes,
                    .operationName = "CTest run"
                });

        return CMakeTestResult{
            .exitCode = process.exitCode,
            .timedOut = process.timedOut,
            .outputTruncated = process.outputTruncated,
            .sourceDirectory = project.sourceDirectory,
            .buildDirectory = project.buildDirectory,
            .ctestExecutable = ctestExecutable,
            .output = process.output,
            .diagnostics = extractSourceDiagnostics(
                process.output,
                project.sourceDirectory)
        };
    }
}
