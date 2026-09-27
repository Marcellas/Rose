#pragma once

#include "platform/SdlRuntime.h"
#include "ui/RoseUiAction.h"
#include "workspace/WorkspaceTypes.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

namespace rose::ui
{
    class SdlRoseContextMenu final
    {
    public:
        explicit SdlRoseContextMenu(platform::SdlRuntime& runtime);
        ~SdlRoseContextMenu();

        SdlRoseContextMenu(const SdlRoseContextMenu&) = delete;
        SdlRoseContextMenu& operator=(const SdlRoseContextMenu&) = delete;

        void setWorkspaceSnapshot(
            workspace::WorkspaceSnapshot snapshot);

        void show(float globalX, float globalY);
        void hide() noexcept;
        [[nodiscard]] bool visible() const noexcept;
        [[nodiscard]] std::optional<RoseUiCommand> handleEvent(const SDL_Event& event);
        void render();

    private:
        struct WindowDeleter { void operator()(SDL_Window* value) const noexcept; };
        struct RendererDeleter { void operator()(SDL_Renderer* value) const noexcept; };
        using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;
        using RendererPtr = std::unique_ptr<SDL_Renderer, RendererDeleter>;

        enum class MenuCategory
        {
            None,
            Chat,
            Projects,
            Capabilities,
            Avatar,
            System
        };

        struct Entry
        {
            Entry(
                std::string entryLabel,
                RoseUiAction entryAction = RoseUiAction::None,
                std::string entryTargetId = {},
                bool entryEnabled = true,
                bool entryHeading = false,
                MenuCategory entryCategory = MenuCategory::None)
                : label{ std::move(entryLabel) }
                , action{ entryAction }
                , targetId{ std::move(entryTargetId) }
                , enabled{ entryEnabled }
                , heading{ entryHeading }
                , category{ entryCategory }
            {
            }

            std::string label;
            RoseUiAction action{ RoseUiAction::None };
            std::string targetId;
            bool enabled{ true };
            bool heading{ false };
            MenuCategory category{ MenuCategory::None };
        };

        [[nodiscard]] int rootEntryIndexAt(float y) const noexcept;
        [[nodiscard]] int submenuEntryIndexAt(float y) const noexcept;
        [[nodiscard]] std::vector<Entry> buildSubmenuEntries(MenuCategory category) const;
        void openSubmenu(MenuCategory category, int rootIndex);
        void hideSubmenu() noexcept;
        void renderEntries(
            SDL_Renderer* renderer,
            const std::vector<Entry>& entries,
            int hoveredIndex,
            int width);

        [[nodiscard]] static std::string clippedLabel(
            std::string_view text,
            std::size_t maximumCharacters);

        platform::SdlRuntime& runtime_;
        WindowPtr window_;
        RendererPtr renderer_;
        WindowPtr submenuWindow_;
        RendererPtr submenuRenderer_;

        std::vector<Entry> rootEntries_;
        std::vector<Entry> submenuEntries_;
        workspace::WorkspaceSnapshot workspaceSnapshot_;

        bool visible_{ false };
        bool submenuVisible_{ false };
        int hoveredRootIndex_{ -1 };
        int hoveredSubmenuIndex_{ -1 };
        int rootGlobalX_{ 0 };
        int rootGlobalY_{ 0 };
        MenuCategory activeCategory_{ MenuCategory::None };

        static constexpr int width_{ 232 };
        static constexpr int submenuWidth_{ 356 };
        static constexpr int rowHeight_{ 18 };
    };
}
