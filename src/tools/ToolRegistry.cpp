#include "tools/ToolRegistry.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace rose::tools
{

    void ToolRegistry::registerTool(
        std::unique_ptr<ITool> tool)
    {
        if (!tool)
        {
            throw std::invalid_argument{
                "ToolRegistry cannot register a null tool."
            };
        }

        const std::string id =
            tool->descriptor().id;

        if (id.empty())
        {
            throw std::invalid_argument{
                "ToolRegistry requires every tool to have a non-empty id."
            };
        }

        if (tools_.contains(id))
        {
            throw std::runtime_error{
                "ToolRegistry already contains tool id '"
                + id
                + "'."
            };
        }

        tools_.emplace(
            id,
            std::move(tool));
    }


    ITool* ToolRegistry::find(
        const std::string_view toolId) noexcept
    {
        const auto found =
            tools_.find(
                std::string{ toolId });

        return found == tools_.end()
            ? nullptr
            : found->second.get();
    }


    const ITool* ToolRegistry::find(
        const std::string_view toolId) const noexcept
    {
        const auto found =
            tools_.find(
                std::string{ toolId });

        return found == tools_.end()
            ? nullptr
            : found->second.get();
    }


    ToolResult ToolRegistry::execute(
        const ToolRequest& request)
    {
        if (request.toolId.empty())
        {
            throw std::invalid_argument{
                "ToolRequest requires a tool id."
            };
        }

        ITool* tool =
            find(request.toolId);

        if (tool == nullptr)
        {
            throw std::runtime_error{
                "No registered Rose tool has id '"
                + request.toolId
                + "'."
            };
        }

        return tool->execute(request);
    }


    std::vector<ToolDescriptor> ToolRegistry::descriptors() const
    {
        std::vector<ToolDescriptor> result;
        result.reserve(tools_.size());

        for (const auto& [id, tool] : tools_)
        {
            (void)id;
            result.push_back(tool->descriptor());
        }

        return result;
    }

} // namespace rose::tools
