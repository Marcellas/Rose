#include "tools/LaunchProgramTool.h"

#include "process/ProcessService.h"
#include "shortcuts/ShortcutService.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]] std::string lowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        [[nodiscard]] const std::string& requiredArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            if (found == request.arguments.end() || found->second.empty())
                throw std::invalid_argument{ "Tool '" + request.toolId + "' requires argument '" + std::string{ name } + "'." };
            return found->second;
        }

        [[nodiscard]] std::string optionalArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            return found == request.arguments.end() ? std::string{} : found->second;
        }

        void rejectUnknownArguments(const ToolRequest& request)
        {
            for (const auto& [name, value] : request.arguments)
            {
                (void)value;
                if (name != "path" && name != "arguments" && name != "working_directory")
                    throw std::invalid_argument{ "launch_program does not accept argument '" + name + "'." };
            }
        }
    }

    LaunchProgramTool::LaunchProgramTool(
        process::IProcessService& processService,
        shortcuts::IShortcutService& shortcutService)
        : processService_{ processService }
        , shortcutService_{ shortcutService }
        , descriptor_{
            .id = "launch_program",
            .displayName = "Launch Program",
            .description =
                "Launch one exact local Windows executable or .lnk shortcut. The exact target is confirmation-gated, "
                "no command shell is invoked, and Internet .url shortcuts are intentionally excluded.",
            .risk = ToolRisk::ExternalEffect,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "path", .description = "Absolute path of the exact .exe or .lnk to launch.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "arguments", .description = "Optional command-line arguments explicitly requested for the program.", .type = ToolValueType::String, .required = false },
                ToolParameterDescriptor{ .name = "working_directory", .description = "Optional absolute working directory.", .type = ToolValueType::String, .required = false }
            }
        }
    {
    }

    const ToolDescriptor& LaunchProgramTool::descriptor() const noexcept { return descriptor_; }

    ToolResult LaunchProgramTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
            throw std::invalid_argument{ "LaunchProgramTool received a request for a different tool." };
        rejectUnknownArguments(request);

        std::filesystem::path requestedPath{ requiredArgument(request, "path") };
        if (!requestedPath.is_absolute())
            throw std::invalid_argument{ "launch_program requires an absolute .exe or .lnk path." };
        requestedPath = requestedPath.lexically_normal();

        const std::string extension = lowerAscii(requestedPath.extension().string());
        process::LaunchProcessRequest launchRequest;
        launchRequest.arguments = optionalArgument(request, "arguments");

        const std::string explicitWorkingDirectory = optionalArgument(request, "working_directory");
        if (!explicitWorkingDirectory.empty())
        {
            launchRequest.workingDirectory = std::filesystem::path{ explicitWorkingDirectory }.lexically_normal();
            if (!launchRequest.workingDirectory->is_absolute())
                throw std::invalid_argument{ "launch_program working_directory must be absolute." };
        }

        std::string launchedFrom = requestedPath.string();
        if (extension == ".lnk")
        {
            const shortcuts::ShortcutInspection shortcut = shortcutService_.inspect(requestedPath);
            if (shortcut.target.empty())
                throw std::runtime_error{ "The .lnk shortcut has no executable target." };

            launchRequest.executablePath = std::filesystem::path{ shortcut.target }.lexically_normal();
            if (!launchRequest.executablePath.is_absolute())
                throw std::runtime_error{ "The .lnk shortcut target is not an absolute path." };
            if (lowerAscii(launchRequest.executablePath.extension().string()) != ".exe")
                throw std::runtime_error{ "launch_program currently allows .lnk shortcuts only when they resolve to an .exe target." };

            if (!shortcut.arguments.empty())
            {
                launchRequest.arguments = shortcut.arguments
                    + (launchRequest.arguments.empty() ? std::string{} : " " + launchRequest.arguments);
            }
            if (!launchRequest.workingDirectory.has_value() && !shortcut.workingDirectory.empty())
            {
                std::filesystem::path shortcutWorkingDirectory{ shortcut.workingDirectory };
                if (shortcutWorkingDirectory.is_absolute())
                    launchRequest.workingDirectory = shortcutWorkingDirectory.lexically_normal();
            }
        }
        else if (extension == ".exe")
        {
            launchRequest.executablePath = requestedPath;
        }
        else
        {
            throw std::invalid_argument{ "launch_program supports exact .exe files and .lnk shortcuts only." };
        }

        const process::LaunchProcessResult result = processService_.launch(launchRequest);
        std::ostringstream message;
        message << "Launched program:\nrequested_path=" << launchedFrom
                << "\nexecutable=" << result.executablePath.string()
                << "\npid=" << result.processId;
        if (!launchRequest.arguments.empty()) message << "\narguments=" << launchRequest.arguments;
        if (launchRequest.workingDirectory.has_value()) message << "\nworking_directory=" << launchRequest.workingDirectory->string();

        return ToolResult{
            .success = true,
            .message = message.str(),
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
