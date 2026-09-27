#include "workspace/WorkspaceCommand.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace rose::workspace
{
    namespace
    {
        [[nodiscard]]
        std::string trim(const std::string_view text)
        {
            std::size_t first{ 0 };
            while (first < text.size()
                && std::isspace(static_cast<unsigned char>(text[first])) != 0)
            {
                ++first;
            }

            std::size_t last = text.size();
            while (last > first
                && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0)
            {
                --last;
            }

            return std::string{ text.substr(first, last - first) };
        }


        [[nodiscard]]
        bool startsWith(
            const std::string_view text,
            const std::string_view prefix) noexcept
        {
            return text.size() >= prefix.size()
                && text.substr(0, prefix.size()) == prefix;
        }


        [[noreturn]]
        void throwDiscussionUsage()
        {
            throw std::invalid_argument{
                "Discussion commands: /discussions, /discussion remove [discussion-id], "
                "/discussion restore <discussion-id>"
            };
        }


        [[noreturn]]
        void throwProjectUsage()
        {
            throw std::invalid_argument{
                "Project commands: /projects, /project remove [project-id], "
                "/project restore <project-id>"
            };
        }
    }


    std::optional<WorkspaceCommand> parseWorkspaceCommand(
        const std::string_view text)
    {
        if (text == "/discussions")
        {
            return WorkspaceCommand{
                .kind = WorkspaceCommandKind::ListDiscussions,
                .targetId = {}
            };
        }

        if (text == "/projects")
        {
            return WorkspaceCommand{
                .kind = WorkspaceCommandKind::ListProjects,
                .targetId = {}
            };
        }

        constexpr std::string_view discussionRemove{ "/discussion remove" };
        constexpr std::string_view discussionRestore{ "/discussion restore" };
        constexpr std::string_view projectRemove{ "/project remove" };
        constexpr std::string_view projectRestore{ "/project restore" };

        if (text == discussionRemove)
        {
            return WorkspaceCommand{
                .kind = WorkspaceCommandKind::RemoveDiscussion,
                .targetId = {}
            };
        }

        if (startsWith(text, std::string{ discussionRemove } + " "))
        {
            const std::string target = trim(text.substr(discussionRemove.size()));
            if (target.empty())
            {
                throwDiscussionUsage();
            }
            return WorkspaceCommand{
                .kind = WorkspaceCommandKind::RemoveDiscussion,
                .targetId = target
            };
        }

        if (startsWith(text, std::string{ discussionRestore } + " "))
        {
            const std::string target = trim(text.substr(discussionRestore.size()));
            if (target.empty())
            {
                throwDiscussionUsage();
            }
            return WorkspaceCommand{
                .kind = WorkspaceCommandKind::RestoreDiscussion,
                .targetId = target
            };
        }

        if (text == discussionRestore || startsWith(text, "/discussion"))
        {
            throwDiscussionUsage();
        }

        if (text == projectRemove)
        {
            return WorkspaceCommand{
                .kind = WorkspaceCommandKind::RemoveProject,
                .targetId = {}
            };
        }

        if (startsWith(text, std::string{ projectRemove } + " "))
        {
            const std::string target = trim(text.substr(projectRemove.size()));
            if (target.empty())
            {
                throwProjectUsage();
            }
            return WorkspaceCommand{
                .kind = WorkspaceCommandKind::RemoveProject,
                .targetId = target
            };
        }

        if (startsWith(text, std::string{ projectRestore } + " "))
        {
            const std::string target = trim(text.substr(projectRestore.size()));
            if (target.empty())
            {
                throwProjectUsage();
            }
            return WorkspaceCommand{
                .kind = WorkspaceCommandKind::RestoreProject,
                .targetId = target
            };
        }

        if (text == projectRestore || startsWith(text, "/project"))
        {
            throwProjectUsage();
        }

        return std::nullopt;
    }

} // namespace rose::workspace
