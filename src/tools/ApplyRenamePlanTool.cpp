#include "tools/ApplyRenamePlanTool.h"

#include "tools/BatchMovePathsTool.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rose::tools
{
    namespace
    {
        struct PlanOperation
        {
            std::filesystem::path source;
            std::filesystem::path destination;
        };


        [[nodiscard]]
        const std::string& requiredArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found = request.arguments.find(std::string{ name });
            if (found == request.arguments.end() || found->second.empty())
            {
                throw std::invalid_argument{
                    "Tool '" + request.toolId + "' requires argument '"
                    + std::string{ name } + "'."
                };
            }
            return found->second;
        }


        [[nodiscard]]
        bool isUnder(
            const std::filesystem::path& child,
            const std::filesystem::path& parent)
        {
            const std::filesystem::path normalizedChild = child.lexically_normal();
            const std::filesystem::path normalizedParent = parent.lexically_normal();

            auto childIt = normalizedChild.begin();
            auto parentIt = normalizedParent.begin();

            for (; parentIt != normalizedParent.end(); ++parentIt, ++childIt)
            {
                if (childIt == normalizedChild.end())
                {
                    return false;
                }

#ifdef _WIN32
                std::string left = childIt->string();
                std::string right = parentIt->string();
                std::transform(left.begin(), left.end(), left.begin(),
                    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::transform(right.begin(), right.end(), right.begin(),
                    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (left != right)
                {
                    return false;
                }
#else
                if (*childIt != *parentIt)
                {
                    return false;
                }
#endif
            }

            return true;
        }


        [[nodiscard]]
        std::vector<std::string> splitTabs(
            const std::string& line)
        {
            std::vector<std::string> fields;
            std::size_t begin{ 0 };

            while (begin <= line.size())
            {
                const std::size_t separator = line.find('\t', begin);
                fields.push_back(
                    line.substr(
                        begin,
                        separator == std::string::npos
                            ? std::string::npos
                            : separator - begin));

                if (separator == std::string::npos)
                {
                    break;
                }

                begin = separator + 1;
            }

            return fields;
        }
    } // namespace


    ApplyRenamePlanTool::ApplyRenamePlanTool(
        std::filesystem::path planDirectory,
        const ApplyRenamePlanToolConfig config)
        : planDirectory_{ std::move(planDirectory) }
        , config_{ config }
        , descriptor_{
            .id = "apply_rename_plan",
            .displayName = "Apply Rename Plan",
            .description =
                "Apply one Rose-owned directory rename plan after explicit confirmation. "
                "The plan is revalidated, all destinations are preflighted, existing files "
                "are never overwritten, and the underlying batch mover attempts rollback if "
                "a later rename fails.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "plan_path",
                    .description = "Exact Rose-owned .roseplan path returned by plan_directory_document_renames.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
        if (planDirectory_.empty() || config_.maximumOperations == 0)
        {
            throw std::invalid_argument{
                "ApplyRenamePlanTool requires a plan directory and non-zero operation limit."
            };
        }
    }


    const ToolDescriptor& ApplyRenamePlanTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult ApplyRenamePlanTool::execute(
        const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
        {
            throw std::invalid_argument{
                "ApplyRenamePlanTool received a request for a different tool."
            };
        }

        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "plan_path")
            {
                throw std::invalid_argument{
                    "Tool 'apply_rename_plan' does not accept argument '" + name + "'."
                };
            }
        }

        std::filesystem::path planPath{ requiredArgument(request, "plan_path") };
        if (!planPath.is_absolute())
        {
            throw std::invalid_argument{
                "apply_rename_plan requires an absolute Rose plan path."
            };
        }
        planPath = planPath.lexically_normal();

        std::filesystem::path allowedDirectory = planDirectory_;
        if (!allowedDirectory.is_absolute())
        {
            allowedDirectory = std::filesystem::absolute(allowedDirectory);
        }
        allowedDirectory = allowedDirectory.lexically_normal();

        if (!isUnder(planPath, allowedDirectory) || planPath.extension() != ".roseplan")
        {
            throw std::invalid_argument{
                "apply_rename_plan accepts only .roseplan files inside Rose's plan directory."
            };
        }

        std::ifstream input{ planPath, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{
                "Could not open Rose rename plan: " + planPath.string()
            };
        }

        std::string line;
        if (!std::getline(input, line) || line != "ROSE_RENAME_PLAN_V1")
        {
            throw std::runtime_error{
                "Rename plan has an unsupported or invalid header."
            };
        }

        std::filesystem::path root;
        std::vector<PlanOperation> operations;

        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            const std::vector<std::string> fields = splitTabs(line);
            if (fields.empty())
            {
                continue;
            }

            if (fields[0] == "ROOT" && fields.size() == 2)
            {
                root = std::filesystem::path{ fields[1] }.lexically_normal();
                continue;
            }

            if (fields[0] == "OP" && fields.size() == 3)
            {
                operations.push_back(
                    PlanOperation{
                        .source = std::filesystem::path{ fields[1] }.lexically_normal(),
                        .destination = std::filesystem::path{ fields[2] }.lexically_normal()
                    });

                if (operations.size() > config_.maximumOperations)
                {
                    throw std::runtime_error{
                        "Rename plan exceeds Rose's configured operation limit."
                    };
                }
            }
        }

        if (root.empty() || !root.is_absolute())
        {
            throw std::runtime_error{
                "Rename plan is missing a valid absolute ROOT."
            };
        }

        if (operations.empty())
        {
            throw std::runtime_error{
                "Rename plan contains no executable operations."
            };
        }

        std::ostringstream encoded;
        for (std::size_t index{ 0 }; index < operations.size(); ++index)
        {
            const PlanOperation& operation = operations[index];

            if (
                !operation.source.is_absolute()
                || !operation.destination.is_absolute()
                || !isUnder(operation.source, root)
                || !isUnder(operation.destination, root))
            {
                throw std::runtime_error{
                    "Rename plan contains an operation outside its authorized root."
                };
            }

            if (index != 0)
            {
                encoded << '|';
            }

            encoded
                << operation.source.string()
                << "=>"
                << operation.destination.string();
        }

        BatchMovePathsTool mover{
            BatchMovePathsToolConfig{
                .maximumOperations = config_.maximumOperations
            }
        };

        ToolResult result = mover.execute(
            ToolRequest{
                .toolId = "batch_move_paths",
                .arguments = {
                    { "operations", encoded.str() }
                }
            });

        std::ofstream audit{ planPath, std::ios::binary | std::ios::app };
        if (audit)
        {
            audit << "APPLIED\ttrue\n";
        }

        result.message =
            "Applied Rose rename plan.\nplan_path="
            + planPath.string()
            + "\noperation_count="
            + std::to_string(operations.size());
        result.responseMode = ToolResponseMode::AuthoritativeCompletion;

        return result;
    }

} // namespace rose::tools
