#include "tools/ListProcessesTool.h"

#include "process/ProcessService.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <string>

namespace rose::tools
{
    ListProcessesTool::ListProcessesTool(process::IProcessService& processService)
        : processService_{ processService }
        , descriptor_{
            .id = "list_processes",
            .displayName = "List Running Processes",
            .description = "List a bounded snapshot of local running processes with PID, executable name, image path when accessible, and whether a visible top-level window exists. Read-only.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "limit", .description = "Optional maximum number of process rows, 1-200. Defaults to 80.", .type = ToolValueType::Integer, .required = false }
            }
        }
    {
    }

    const ToolDescriptor& ListProcessesTool::descriptor() const noexcept { return descriptor_; }

    ToolResult ListProcessesTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
            throw std::invalid_argument{ "ListProcessesTool received a request for a different tool." };
        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "limit") throw std::invalid_argument{ "list_processes does not accept argument '" + name + "'." };
        }

        std::size_t limit = 80;
        if (const auto found = request.arguments.find("limit"); found != request.arguments.end() && !found->second.empty())
        {
            unsigned parsed{};
            const auto [end, error] = std::from_chars(found->second.data(), found->second.data() + found->second.size(), parsed);
            if (error != std::errc{} || end != found->second.data() + found->second.size() || parsed == 0 || parsed > 200)
                throw std::invalid_argument{ "list_processes limit must be an integer from 1 to 200." };
            limit = parsed;
        }

        const auto processes = processService_.listProcesses(limit);
        std::ostringstream message;
        message << "Running process snapshot: " << processes.size() << " entr" << (processes.size() == 1 ? "y" : "ies") << ".\n";
        for (const auto& process : processes)
        {
            message << "pid=" << process.processId
                    << " | name=" << process.executableName
                    << " | window=" << (process.hasTopLevelWindow ? "yes" : "no");
            if (!process.imagePath.empty()) message << " | path=" << process.imagePath;
            message << '\n';
        }
        message << "NOTICE: This is read-only process discovery; nothing was launched, closed, or terminated.";

        return ToolResult{ .success = true, .message = message.str(), .artifacts = {} };
    }
}
