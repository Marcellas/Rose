#include "agent/ToolConfirmation.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace rose::agent
{
    namespace
    {
        [[nodiscard]]
        std::string singleLinePreview(
            const std::string_view value,
            const std::size_t maximumCharacters)
        {
            std::string preview;
            preview.reserve(
                (std::min)(
                    value.size(),
                    maximumCharacters));

            for (const char character : value)
            {
                if (preview.size() >= maximumCharacters)
                {
                    break;
                }

                switch (character)
                {
                case '\r':
                case '\n':
                case '\t':
                    preview.push_back(' ');
                    break;

                default:
                    if (
                        std::iscntrl(
                            static_cast<unsigned char>(
                                character)) == 0)
                    {
                        preview.push_back(character);
                    }
                    break;
                }
            }

            if (value.size() > maximumCharacters)
            {
                preview += "...";
            }

            return preview;
        }
    }


    PendingToolConfirmation makePendingToolConfirmation(
        const tools::ToolRequest& request,
        const tools::ToolDescriptor& descriptor)
    {
        std::ostringstream summary;

        summary
            << "Rose wants to run: "
            << descriptor.displayName
            << "\n";

        for (const tools::ToolParameterDescriptor& parameter :
             descriptor.parameters)
        {
            const auto found =
                request.arguments.find(
                    parameter.name);

            if (found == request.arguments.end())
            {
                continue;
            }

            const std::size_t previewLimit =
                parameter.name == "content"
                    ? 240u
                    : 400u;

            summary
                << "- "
                << parameter.name
                << ": "
                << singleLinePreview(
                    found->second,
                    previewLimit)
                << "\n";
        }

        summary
            << "\nType /confirm to execute this exact action, or /cancel to cancel it.";

        return PendingToolConfirmation{
            .request = request,
            .userFacingSummary = summary.str()
        };
    }


    std::string buildToolConfirmationContext(
        const PendingToolConfirmation& pending)
    {
        std::ostringstream text;

        text
            << "<rose_tool_confirmation_required>\n"
            << "This block is trusted metadata produced by Rose's permission layer.\n"
            << "The proposed tool action has NOT been executed.\n"
            << "The user must explicitly confirm the exact pending request before execution.\n"
            << "tool_id="
            << pending.request.toolId
            << "\n"
            << "summary_begin\n"
            << pending.userFacingSummary
            << "\nsummary_end\n"
            << "</rose_tool_confirmation_required>\n"
            << "Tell the user that this action needs confirmation. Do not claim the action "
               "already happened. Ask them to use /confirm or /cancel.";

        return text.str();
    }

} // namespace rose::agent
