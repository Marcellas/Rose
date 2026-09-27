#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rose::workspace
{
    enum class WorkspaceCommandKind
    {
        ListDiscussions,
        RemoveDiscussion,
        RestoreDiscussion,
        ListProjects,
        RemoveProject,
        RestoreProject
    };


    struct WorkspaceCommand
    {
        WorkspaceCommandKind kind{ WorkspaceCommandKind::ListDiscussions };

        // Remove accepts an empty target to mean "the active item". Restore
        // always requires an explicit durable id because removed records are no
        // longer eligible to be active.
        std::string targetId;
    };


    [[nodiscard]]
    std::optional<WorkspaceCommand> parseWorkspaceCommand(
        std::string_view text);

} // namespace rose::workspace
