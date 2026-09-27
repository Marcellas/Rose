#include "tools/InspectShortcutRegisteredTool.h"

#include "files/FileFormatCatalog.h"
#include "permissions/PermissionSystem.h"
#include "shortcuts/ShortcutService.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace rose::tools
{
    InspectShortcutRegisteredTool::InspectShortcutRegisteredTool(
        permissions::PermissionSystem& permissions,
        shortcuts::IShortcutService& shortcutService)
        : permissions_{ permissions }
        , shortcutService_{ shortcutService }
        , descriptor_{
            .id = "inspect_shortcut",
            .displayName = "Inspect Shortcut",
            .description = "Resolve one exact Windows .lnk or Internet .url shortcut without launching it. Returns target/URL, arguments, working directory, description, and icon metadata when available.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "path", .description = "Absolute path of the exact .lnk or .url file to inspect.", .type = ToolValueType::String, .required = true }
            }
        }
    {
    }

    const ToolDescriptor& InspectShortcutRegisteredTool::descriptor() const noexcept { return descriptor_; }

    ToolResult InspectShortcutRegisteredTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
            throw std::invalid_argument{ "InspectShortcutRegisteredTool received a different tool id." };
        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "path") throw std::invalid_argument{ "inspect_shortcut does not accept argument '" + name + "'." };
        }
        const auto found = request.arguments.find("path");
        if (found == request.arguments.end() || found->second.empty())
            throw std::invalid_argument{ "inspect_shortcut requires argument 'path'." };
        const std::filesystem::path path{ found->second };
        if (!path.is_absolute()) throw std::invalid_argument{ "inspect_shortcut requires an absolute file path." };
        if (files::classifyFileFormat(path).kind != files::FileFormatKind::Shortcut)
            throw std::invalid_argument{ "inspect_shortcut supports .lnk and .url files only." };

        permissions_.grantReadOnce(path);
        if (!permissions_.consumeReadOnce(path)) throw std::runtime_error{ "Shortcut read permission could not be established." };
        const shortcuts::ShortcutInspection inspection = shortcutService_.inspect(path);

        std::ostringstream message;
        message << "Inspected shortcut: " << path.string()
                << "\nkind=" << inspection.kind;
        if (!inspection.target.empty()) message << "\ntarget=" << inspection.target;
        if (!inspection.url.empty()) message << "\nurl=" << inspection.url;
        if (!inspection.arguments.empty()) message << "\narguments=" << inspection.arguments;
        if (!inspection.workingDirectory.empty()) message << "\nworking_directory=" << inspection.workingDirectory;
        if (!inspection.description.empty()) message << "\ndescription=" << inspection.description;
        if (!inspection.iconLocation.empty()) message << "\nicon=" << inspection.iconLocation;
        if (!inspection.target.empty() && inspection.url.empty())
            message << "\ntarget_exists=" << (inspection.targetExists ? "true" : "false");
        message << "\nNOTICE: The shortcut was inspected only; Rose did not launch or open its target.";
        return ToolResult{ .success = true, .message = message.str(), .artifacts = {} };
    }
}
