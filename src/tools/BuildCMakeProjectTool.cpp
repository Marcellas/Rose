#include "tools/BuildCMakeProjectTool.h"

#include "development/CMakeBuildService.h"

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
                throw std::invalid_argument{ "build_cmake_project requires argument '" + name + "'." };
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
                throw std::invalid_argument{ "build_cmake_project jobs must be an integer from 1 to 32." };
            }
            return static_cast<std::size_t>(parsed);
        }
    }

    BuildCMakeProjectTool::BuildCMakeProjectTool(
        development::ICMakeBuildService& service)
        : service_{ service }
        , descriptor_{
            .id = "build_cmake_project",
            .displayName = "Build Configured CMake Project",
            .description =
                "Build one exact, already-configured local CMake project using cmake.exe directly without a shell. "
                "The build tree is fixed to <source_path>/build and must already contain a CMakeCache.txt that belongs "
                "to that source directory; CMakeCache.txt is created by the configure step, not by --build. The equivalent "
                "CLI shape is cmake --build <source_path>/build --config <configuration> [--target <target>] -j <jobs>. "
                "Build output/diagnostics are captured with bounded size and a 10-minute timeout. Project build rules may "
                "execute code, so this capability is externally consequential and confirmation-gated.",
            .risk = ToolRisk::ExternalEffect,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "source_path", .description = "Exact absolute CMake source directory containing CMakeLists.txt. Rose uses its existing <source_path>/build tree.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "configuration", .description = "Optional CMake configuration: Debug, Release, RelWithDebInfo, or MinSizeRel. Defaults to Debug.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "target", .description = "Optional exact CMake build target. Omit to build the configured default target set. Installation/package/deploy-style targets are intentionally rejected.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "jobs", .description = "Optional parallel build job count from 1 to 32. Defaults to 8.", .type = ToolValueType::Integer, .required = false }
            }
        }
    {
    }

    const ToolDescriptor& BuildCMakeProjectTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult BuildCMakeProjectTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
            throw std::invalid_argument{ "BuildCMakeProjectTool received a request for a different tool." };

        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "source_path"
                && name != "configuration"
                && name != "target"
                && name != "jobs")
            {
                throw std::invalid_argument{ "build_cmake_project does not accept argument '" + name + "'." };
            }
        }

        development::CMakeBuildRequest buildRequest;
        buildRequest.sourceDirectory = std::filesystem::path{ requiredArgument(request, "source_path") }.lexically_normal();
        if (!buildRequest.sourceDirectory.is_absolute())
            throw std::invalid_argument{ "build_cmake_project source_path must be absolute." };

        buildRequest.configuration = optionalArgument(request, "configuration");
        if (buildRequest.configuration.empty()) buildRequest.configuration = "Debug";
        buildRequest.target = optionalArgument(request, "target");
        buildRequest.parallelJobs = parseJobs(request);

        const development::CMakeBuildResult result = service_.build(buildRequest);
        const bool success = !result.timedOut && result.exitCode == 0;

        std::ostringstream message;
        message << "CMake build completed:\n"
                << "source_path=" << result.sourceDirectory.string() << '\n'
                << "build_path=" << result.buildDirectory.string() << '\n'
                << "cmake_executable=" << result.cmakeExecutable.string() << '\n'
                << "configuration=" << buildRequest.configuration << '\n';
        if (!buildRequest.target.empty()) message << "target=" << buildRequest.target << '\n';
        message << "jobs=" << buildRequest.parallelJobs << '\n'
                << "exit_code=" << result.exitCode << '\n'
                << "timed_out=" << (result.timedOut ? "true" : "false") << '\n'
                << "output_truncated=" << (result.outputTruncated ? "true" : "false") << '\n'
                << "build_success=" << (success ? "true" : "false") << '\n'
                << "output_begin\n"
                << result.output
                << "\noutput_end";

        return ToolResult{
            .success = success,
            .message = message.str(),
            .trustedMetadata = development::buildTrustedDiagnosticMetadata(
                "build_cmake_project",
                success,
                result.diagnostics),
            .responseMode = ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };
    }
}
