#include "tools/RunCMakeTestsTool.h"

#include "development/CMakeTestService.h"

#include <charconv>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]]
        std::string requiredArgument(
            const ToolRequest& request,
            const std::string& name)
        {
            const auto found = request.arguments.find(name);
            if (found == request.arguments.end() || found->second.empty())
                throw std::invalid_argument{ "run_cmake_tests requires argument '" + name + "'." };
            return found->second;
        }

        [[nodiscard]]
        std::string optionalArgument(
            const ToolRequest& request,
            const std::string& name)
        {
            const auto found = request.arguments.find(name);
            return found == request.arguments.end() ? std::string{} : found->second;
        }

        [[nodiscard]]
        std::size_t parseJobs(const ToolRequest& request)
        {
            const std::string value = optionalArgument(request, "jobs");
            if (value.empty()) return 8;

            unsigned parsed{};
            const auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{}
                || end != value.data() + value.size()
                || parsed == 0
                || parsed > 32)
            {
                throw std::invalid_argument{ "run_cmake_tests jobs must be an integer from 1 to 32." };
            }

            return static_cast<std::size_t>(parsed);
        }
    }

    RunCMakeTestsTool::RunCMakeTestsTool(
        development::ICMakeTestService& service)
        : service_{ service }
        , descriptor_{
            .id = "run_cmake_tests",
            .displayName = "Run Registered CMake Tests",
            .description =
                "Run tests already registered with CTest in one exact, already-configured local CMake project. "
                "Rose invokes ctest.exe directly without a shell, uses only <source_path>/build after verifying its "
                "CMakeCache belongs to that source, and can run either all registered tests or one exact named test. "
                "Output, per-test runtime, total runtime, and parallelism are bounded. CTest entries execute project "
                "code, so this capability is externally consequential and always confirmation-gated.",
            .risk = ToolRisk::ExternalEffect,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "source_path", .description = "Exact absolute CMake source directory containing CMakeLists.txt. Rose uses its existing <source_path>/build CTest registration.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "configuration", .description = "Optional CTest configuration: Debug, Release, RelWithDebInfo, or MinSizeRel. Defaults to Debug.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "test", .description = "Optional exact registered CTest name. Omit to run all registered tests.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "jobs", .description = "Optional parallel CTest job count from 1 to 32. Defaults to 8.", .type = ToolValueType::Integer, .required = false }
            }
        }
    {
    }

    const ToolDescriptor& RunCMakeTestsTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult RunCMakeTestsTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
            throw std::invalid_argument{ "RunCMakeTestsTool received a request for a different tool." };

        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "source_path"
                && name != "configuration"
                && name != "test"
                && name != "jobs")
            {
                throw std::invalid_argument{ "run_cmake_tests does not accept argument '" + name + "'." };
            }
        }

        development::CMakeTestRequest testRequest;
        testRequest.sourceDirectory =
            std::filesystem::path{ requiredArgument(request, "source_path") }.lexically_normal();
        if (!testRequest.sourceDirectory.is_absolute())
            throw std::invalid_argument{ "run_cmake_tests source_path must be absolute." };

        testRequest.configuration = optionalArgument(request, "configuration");
        if (testRequest.configuration.empty()) testRequest.configuration = "Debug";
        testRequest.testName = optionalArgument(request, "test");
        testRequest.parallelJobs = parseJobs(request);

        const development::CMakeTestResult result = service_.run(testRequest);
        const bool success = !result.timedOut && result.exitCode == 0;

        std::ostringstream message;
        message << "CTest run completed:\n"
                << "source_path=" << result.sourceDirectory.string() << '\n'
                << "build_path=" << result.buildDirectory.string() << '\n'
                << "ctest_executable=" << result.ctestExecutable.string() << '\n'
                << "configuration=" << testRequest.configuration << '\n';
        if (!testRequest.testName.empty())
            message << "test=" << testRequest.testName << '\n';
        else
            message << "test=ALL_REGISTERED\n";
        message << "jobs=" << testRequest.parallelJobs << '\n'
                << "exit_code=" << result.exitCode << '\n'
                << "timed_out=" << (result.timedOut ? "true" : "false") << '\n'
                << "output_truncated=" << (result.outputTruncated ? "true" : "false") << '\n'
                << "tests_success=" << (success ? "true" : "false") << '\n'
                << "output_begin\n"
                << result.output
                << "\noutput_end";

        return ToolResult{
            .success = success,
            .message = message.str(),
            .trustedMetadata = development::buildTrustedDiagnosticMetadata(
                "run_cmake_tests",
                success,
                result.diagnostics),
            .responseMode = ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };
    }
}
