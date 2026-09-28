#include "tools/ReconfigureCMakeProjectTool.h"

#include "development/CMakeConfigureService.h"
#include "development/DiagnosticExtraction.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]]
        const std::string& requiredArgument(
            const ToolRequest& request,
            const std::string& name)
        {
            const auto found = request.arguments.find(name);
            if (found == request.arguments.end() || found->second.empty())
            {
                throw std::invalid_argument{
                    "reconfigure_cmake_project requires argument '" + name + "'."
                };
            }
            return found->second;
        }
    }


    ReconfigureCMakeProjectTool::ReconfigureCMakeProjectTool(
        development::ICMakeConfigureService& service)
        : service_{ service }
        , descriptor_{
            .id = "reconfigure_cmake_project",
            .displayName = "Reconfigure Existing CMake Project",
            .description =
                "Re-run CMake configure/generate for one exact local project using only its EXISTING "
                "<source_path>/build tree after verifying CMakeCache.txt belongs to that source. Rose invokes "
                "cmake.exe directly as '-S <source> -B <source>/build' without a shell. This tool cannot create "
                "a new build tree, change generators/toolchains, inject -D variables, use presets, install, "
                "package, or deploy. Configure scripts and dependency discovery may execute project-controlled "
                "logic or network activity, so reconfiguration is externally consequential and confirmation-gated.",
            .risk = ToolRisk::ExternalEffect,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "source_path",
                    .description =
                        "Exact absolute CMake source directory. Its existing <source_path>/build/CMakeCache.txt "
                        "must already identify the same source tree.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
    }


    const ToolDescriptor& ReconfigureCMakeProjectTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult ReconfigureCMakeProjectTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "ReconfigureCMakeProjectTool received a request for a different tool."
            };
        }

        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "source_path")
            {
                throw std::invalid_argument{
                    "reconfigure_cmake_project does not accept argument '" + name + "'."
                };
            }
        }

        development::CMakeConfigureRequest configureRequest{
            .sourceDirectory = std::filesystem::path{
                requiredArgument(request, "source_path")
            }.lexically_normal()
        };

        if (!configureRequest.sourceDirectory.is_absolute())
        {
            throw std::invalid_argument{
                "reconfigure_cmake_project source_path must be absolute."
            };
        }

        const development::CMakeConfigureResult result =
            service_.reconfigure(configureRequest);

        const bool success =
            !result.timedOut
            && result.exitCode == 0;

        std::ostringstream message;
        message
            << "CMake reconfiguration completed:\n"
            << "source_path=" << result.sourceDirectory.string() << '\n'
            << "build_path=" << result.buildDirectory.string() << '\n'
            << "cmake_executable=" << result.cmakeExecutable.string() << '\n'
            << "exit_code=" << result.exitCode << '\n'
            << "timed_out=" << (result.timedOut ? "true" : "false") << '\n'
            << "output_truncated=" << (result.outputTruncated ? "true" : "false") << '\n'
            << "configure_success=" << (success ? "true" : "false") << '\n'
            << "output_begin\n"
            << result.output
            << "\noutput_end";

        return ToolResult{
            .success = success,
            .message = message.str(),
            .trustedMetadata = development::buildTrustedDiagnosticMetadata(
                "reconfigure_cmake_project",
                success,
                result.diagnostics),
            .responseMode = ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };
    }

} // namespace rose::tools
