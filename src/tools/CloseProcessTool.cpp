#include "tools/CloseProcessTool.h"

#include "process/ProcessService.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace rose::tools
{
    CloseProcessTool::CloseProcessTool(process::IProcessService& processService)
        : processService_{ processService }
        , descriptor_{
            .id = "close_process",
            .displayName = "Close Process Gracefully",
            .description =
                "Request that one exact local GUI process close by sending WM_CLOSE to its visible top-level windows. "
                "The application may prompt to save or refuse. Rose never force-terminates the process through this tool.",
            .risk = ToolRisk::Destructive,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "pid", .description = "Exact numeric process id to request closure for.", .type = ToolValueType::Integer, .required = true }
            }
        }
    {
    }

    const ToolDescriptor& CloseProcessTool::descriptor() const noexcept { return descriptor_; }

    ToolResult CloseProcessTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
            throw std::invalid_argument{ "CloseProcessTool received a request for a different tool." };
        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "pid") throw std::invalid_argument{ "close_process does not accept argument '" + name + "'." };
        }
        const auto found = request.arguments.find("pid");
        if (found == request.arguments.end() || found->second.empty())
            throw std::invalid_argument{ "close_process requires argument 'pid'." };

        std::uint64_t parsed{};
        const auto [end, error] = std::from_chars(found->second.data(), found->second.data() + found->second.size(), parsed);
        if (error != std::errc{} || end != found->second.data() + found->second.size()
            || parsed == 0 || parsed > (std::numeric_limits<std::uint32_t>::max)())
            throw std::invalid_argument{ "close_process pid must be a non-zero 32-bit integer." };

        const process::CloseProcessResult result = processService_.requestClose(static_cast<std::uint32_t>(parsed));
        std::ostringstream message;
        message << "Requested graceful process close:\npid=" << result.processId;
        if (!result.executableName.empty()) message << "\nname=" << result.executableName;
        if (!result.imagePath.empty()) message << "\npath=" << result.imagePath;
        message << "\nwindows_notified=" << result.windowsNotified
                << "\nexited_within_wait=" << (result.exited ? "true" : "false");
        if (!result.exited)
            message << "\nNOTICE: Rose sent WM_CLOSE only. The process may still be showing a save/confirmation prompt or may have ignored the close request; Rose did not force-terminate it.";

        return ToolResult{
            .success = true,
            .message = message.str(),
            .responseMode = ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }
}
