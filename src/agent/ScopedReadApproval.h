#pragma once

#include "tools/ToolTypes.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

namespace rose::agent
{
    struct ScopedReadApproval
    {
        std::filesystem::path path;
        bool directory{ false };
    };

    [[nodiscard]] inline bool isScopedReadTool(const std::string_view id)
    {
        constexpr std::string_view allowed[] = {
            "list_directory", "scan_directory_tree", "search_local_files",
            "read_text_file", "read_pdf", "read_office_document",
            "inspect_image", "inspect_media", "inspect_database",
            "inspect_shortcut", "list_zip_archive",
            "analyze_directory_documents"
        };
        return std::find(std::begin(allowed), std::end(allowed), id)
            != std::end(allowed);
    }

    // Called only after an exact confirmed read succeeds. The approved directory
    // or exact file is held in the current AgentRunState and never persisted.
    [[nodiscard]] inline std::optional<ScopedReadApproval> approvedReadScope(
        const tools::ToolRequest& request,
        const tools::ToolDescriptor& descriptor)
    {
        if (descriptor.risk != tools::ToolRisk::ReadOnly
            || !isScopedReadTool(request.toolId)) return std::nullopt;
        const auto it = request.arguments.find("path");
        if (it == request.arguments.end()) return std::nullopt;
        const std::filesystem::path requested{ it->second };
        if (!requested.is_absolute()) return std::nullopt;
        std::error_code error;
        const auto canonical = std::filesystem::canonical(requested, error);
        if (error) return std::nullopt;
        const bool directory = std::filesystem::is_directory(canonical, error);
        if (error) return std::nullopt;
        return ScopedReadApproval{ canonical, directory };
    }

    [[nodiscard]] inline bool coveredByReadScope(
        const ScopedReadApproval& scope,
        const tools::ToolRequest& request,
        const tools::ToolDescriptor& descriptor)
    {
        if (descriptor.risk != tools::ToolRisk::ReadOnly
            || !isScopedReadTool(request.toolId)) return false;
        const auto it = request.arguments.find("path");
        if (it == request.arguments.end()) return false;
        const std::filesystem::path requested{ it->second };
        if (!requested.is_absolute()) return false;

        std::error_code error;
        const auto canonical = std::filesystem::canonical(requested, error);
        if (error) return false;
        if (canonical == scope.path) return true;
        if (!scope.directory) return false;
        const auto relative = canonical.lexically_relative(scope.path);
        return !relative.empty() && *relative.begin() != "..";
    }
}
