#pragma once

#include "tools/ITool.h"

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rose::tools
{

    // ToolRegistry owns registered tool adapters.
    //
    // The adapters may themselves borrow longer-lived services, such as
    // GenerateImageTool borrowing an IImageGenerator and ArtifactStore.
    class ToolRegistry final
    {
    public:
        ToolRegistry() = default;

        ToolRegistry(const ToolRegistry&) = delete;
        ToolRegistry& operator=(const ToolRegistry&) = delete;

        ToolRegistry(ToolRegistry&&) noexcept = default;
        ToolRegistry& operator=(ToolRegistry&&) noexcept = default;

        void registerTool(
            std::unique_ptr<ITool> tool);

        // Ownership-safe inverse of registerTool(). The caller receives the
        // removed adapter and decides whether to destroy, reconfigure, or re-register
        // it. Returns nullptr when the id is not registered.
        [[nodiscard]]
        std::unique_ptr<ITool> removeTool(
            std::string_view toolId) noexcept;

        [[nodiscard]]
        ITool* find(
            std::string_view toolId) noexcept;

        [[nodiscard]]
        const ITool* find(
            std::string_view toolId) const noexcept;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request);

        [[nodiscard]]
        std::vector<ToolDescriptor> descriptors() const;

    private:
        std::unordered_map<std::string, std::unique_ptr<ITool>> tools_;
    };

} // namespace rose::tools
