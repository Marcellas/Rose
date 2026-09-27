#include "agent/ToolConfirmation.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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


        [[nodiscard]]
        std::string formatRenamePlanPreview(
            const std::string_view planPathText)
        {
            const std::filesystem::path planPath{ std::string{ planPathText } };
            std::ifstream input{ planPath, std::ios::binary };

            if (!input)
            {
                return
                    "  plan could not be opened for preview; execution will fail closed "
                    "if it cannot be validated.\n";
            }

            std::ostringstream text;
            std::string line;
            std::size_t operationCount{ 0 };
            std::size_t shown{ 0 };
            constexpr std::size_t maximumShown{ 24 };

            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                if (!line.starts_with("OP\t"))
                {
                    continue;
                }

                ++operationCount;

                if (shown >= maximumShown)
                {
                    continue;
                }

                const std::size_t firstTab = line.find('\t');
                const std::size_t secondTab =
                    firstTab == std::string::npos
                        ? std::string::npos
                        : line.find('\t', firstTab + 1);

                if (secondTab == std::string::npos)
                {
                    continue;
                }

                ++shown;
                text
                    << "  #" << shown << " "
                    << singleLinePreview(
                        std::string_view{ line }.substr(
                            firstTab + 1,
                            secondTab - firstTab - 1),
                        800)
                    << "\n     -> "
                    << singleLinePreview(
                        std::string_view{ line }.substr(secondTab + 1),
                        800)
                    << "\n";
            }

            std::ostringstream header;
            header
                << "  operation_count=" << operationCount << "\n";

            if (operationCount > maximumShown)
            {
                text
                    << "  ... "
                    << (operationCount - maximumShown)
                    << " additional operation(s) are stored in the exact Rose plan above.\n";
            }

            return header.str() + text.str();
        }


        [[nodiscard]]
        std::string formatBatchMoveOperations(
            const std::string_view encoded)
        {
            std::ostringstream text;
            std::size_t begin{ 0 };
            std::size_t index{ 1 };

            while (begin < encoded.size())
            {
                const std::size_t separator =
                    encoded.find('|', begin);

                const std::string_view item =
                    encoded.substr(
                        begin,
                        separator == std::string_view::npos
                            ? std::string_view::npos
                            : separator - begin);

                const std::size_t arrow =
                    item.find("=>");

                text
                    << "  #"
                    << index
                    << " ";

                if (arrow == std::string_view::npos)
                {
                    text
                        << singleLinePreview(
                            item,
                            800);
                }
                else
                {
                    text
                        << singleLinePreview(
                            item.substr(0, arrow),
                            800)
                        << "\n     -> "
                        << singleLinePreview(
                            item.substr(arrow + 2),
                            800);
                }

                text << "\n";

                ++index;

                if (separator == std::string_view::npos)
                {
                    break;
                }

                begin = separator + 1;
            }

            return text.str();
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

            if (
                request.toolId == "apply_rename_plan"
                && parameter.name == "plan_path")
            {
                summary
                    << "- plan_path: "
                    << singleLinePreview(found->second, 800)
                    << "\n- planned rename preview:\n"
                    << formatRenamePlanPreview(found->second);
                continue;
            }

            if (
                request.toolId == "batch_move_paths"
                && parameter.name == "operations")
            {
                summary
                    << "- operations:\n"
                    << formatBatchMoveOperations(
                        found->second);
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
