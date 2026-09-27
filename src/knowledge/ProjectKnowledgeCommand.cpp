#include "knowledge/ProjectKnowledgeCommand.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace rose::knowledge
{
    namespace
    {
        [[nodiscard]]
        std::string trim(
            std::string_view value)
        {
            const auto first = std::find_if(
                value.begin(),
                value.end(),
                [](const unsigned char character)
                {
                    return std::isspace(character) == 0;
                });
            if (first == value.end()) return {};
            const auto last = std::find_if(
                value.rbegin(),
                value.rend(),
                [](const unsigned char character)
                {
                    return std::isspace(character) == 0;
                }).base();
            return std::string{ first, last };
        }

        [[nodiscard]]
        bool startsWith(
            const std::string_view value,
            const std::string_view prefix) noexcept
        {
            return value.size() >= prefix.size()
                && value.substr(0, prefix.size()) == prefix;
        }
    }

    std::optional<ProjectKnowledgeCommand> parseProjectKnowledgeCommand(
        const std::string_view text)
    {
        if (text == "/knowledge" || text == "/knowledge status")
        {
            return ProjectKnowledgeCommand{
                .kind = ProjectKnowledgeCommandKind::Status,
                .text = {}
            };
        }
        if (text == "/knowledge index")
        {
            return ProjectKnowledgeCommand{
                .kind = ProjectKnowledgeCommandKind::Index,
                .text = {}
            };
        }
        if (text == "/knowledge clear")
        {
            return ProjectKnowledgeCommand{
                .kind = ProjectKnowledgeCommandKind::Clear,
                .text = {}
            };
        }

        constexpr std::string_view addRootPrefix{ "/knowledge add-root " };
        if (startsWith(text, addRootPrefix))
        {
            std::string path = trim(text.substr(addRootPrefix.size()));
            if (path.empty())
            {
                throw std::invalid_argument{
                    "Usage: /knowledge add-root <directory>"
                };
            }
            return ProjectKnowledgeCommand{
                .kind = ProjectKnowledgeCommandKind::AddRoot,
                .text = std::move(path)
            };
        }

        constexpr std::string_view removeRootPrefix{ "/knowledge remove-root " };
        if (startsWith(text, removeRootPrefix))
        {
            std::string path = trim(text.substr(removeRootPrefix.size()));
            if (path.empty())
            {
                throw std::invalid_argument{
                    "Usage: /knowledge remove-root <directory>"
                };
            }
            return ProjectKnowledgeCommand{
                .kind = ProjectKnowledgeCommandKind::RemoveRoot,
                .text = std::move(path)
            };
        }

        constexpr std::string_view searchPrefix{ "/knowledge search " };
        if (startsWith(text, searchPrefix))
        {
            std::string query = trim(text.substr(searchPrefix.size()));
            if (query.empty())
            {
                throw std::invalid_argument{
                    "Usage: /knowledge search <query>"
                };
            }
            return ProjectKnowledgeCommand{
                .kind = ProjectKnowledgeCommandKind::Search,
                .text = std::move(query)
            };
        }

        if (startsWith(text, "/knowledge"))
        {
            throw std::invalid_argument{
                "Knowledge commands: /knowledge status, /knowledge add-root <directory>, "
                "/knowledge remove-root <directory>, /knowledge index, "
                "/knowledge search <query>, /knowledge clear"
            };
        }
        return std::nullopt;
    }
}
