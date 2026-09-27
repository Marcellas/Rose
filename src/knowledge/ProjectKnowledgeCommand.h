#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rose::knowledge
{
    enum class ProjectKnowledgeCommandKind
    {
        Status,
        AddRoot,
        RemoveRoot,
        Index,
        Search,
        Clear
    };

    struct ProjectKnowledgeCommand
    {
        ProjectKnowledgeCommandKind kind{
            ProjectKnowledgeCommandKind::Status
        };
        std::string text;
    };

    [[nodiscard]]
    std::optional<ProjectKnowledgeCommand> parseProjectKnowledgeCommand(
        std::string_view text);
}
