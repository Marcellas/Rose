#include "tools/RememberMemoryTool.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]]
        const std::string& requiredArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

            if (
                found == request.arguments.end()
                || found->second.empty())
            {
                throw std::invalid_argument{
                    "Tool '"
                    + request.toolId
                    + "' requires argument '"
                    + std::string{ name }
                    + "'."
                };
            }

            return found->second;
        }


        [[nodiscard]]
        std::string_view trimAsciiWhitespace(
            const std::string_view text) noexcept
        {
            constexpr std::string_view whitespace{
                " \t\r\n"
            };

            const std::size_t first =
                text.find_first_not_of(whitespace);

            if (first == std::string_view::npos)
            {
                return {};
            }

            const std::size_t last =
                text.find_last_not_of(whitespace);

            return text.substr(
                first,
                last - first + 1);
        }


        [[nodiscard]]
        std::string cleanedMemoryContent(
            const std::string_view raw)
        {
            std::string_view text =
                trimAsciiWhitespace(raw);

            // Deterministic recovery may pass the user's whole sentence rather
            // than a model-extracted content argument. Strip only very obvious
            // request wrappers; never rewrite the actual fact/preference.
            static constexpr std::string_view prefixes[]{
                "please remember that ",
                "remember that ",
                "please remember ",
                "remember ",
                "save this memory: ",
                "store this memory: "
            };

            for (const std::string_view prefix : prefixes)
            {
                if (
                    text.size() >= prefix.size())
                {
                    bool matches{ true };

                    for (std::size_t index = 0;
                         index < prefix.size();
                         ++index)
                    {
                        char left = text[index];
                        char right = prefix[index];

                        if (left >= 'A' && left <= 'Z')
                        {
                            left = static_cast<char>(left - 'A' + 'a');
                        }

                        if (left != right)
                        {
                            matches = false;
                            break;
                        }
                    }

                    if (matches)
                    {
                        text =
                            trimAsciiWhitespace(
                                text.substr(prefix.size()));
                        break;
                    }
                }
            }

            return std::string{ text };
        }
    }


    RememberMemoryTool::RememberMemoryTool(
        memory::MemoryRepository& repository)
        : repository_{ repository }
        , descriptor_{
            .id = "remember_memory",
            .displayName = "Remember Memory",
            .description =
                "Store one durable local memory only when the user explicitly asks "
                "Rose to remember, save, or retain a fact, preference, or project "
                "note for future conversations. Do not use for ordinary statements.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::AutoAllowed,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "content",
                    .description =
                        "The fact, preference, or project note to remember. Omit the "
                        "request wrapper such as 'remember that'.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
    }


    const ToolDescriptor&
        RememberMemoryTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult RememberMemoryTool::execute(
        const ToolRequest& request)
    {
        const std::string content =
            cleanedMemoryContent(
                requiredArgument(
                    request,
                    "content"));

        if (content.empty())
        {
            throw std::invalid_argument{
                "The memory content is empty after removing the remember-request wrapper."
            };
        }

        const memory::RememberMemoryResult result =
            repository_.remember(
                content,
                memory::MemoryKind::ExplicitUser,
                "explicit-user-request");

        return ToolResult{
            .success = true,
            .message =
                result.created
                    ? "Saved that to Rose's local long-term memory."
                    : "That is already in Rose's local long-term memory.",
            .responseMode =
                ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }

} // namespace rose::tools
