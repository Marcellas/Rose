#include "ui/SdlRoseContextMenu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace rose::ui
{
    namespace
    {
        constexpr std::size_t maximumRecentEntries{ 8 };
    }


    SdlRoseContextMenu::SdlRoseContextMenu(platform::SdlRuntime& runtime)
        : runtime_{ runtime }
    {
        rootEntries_ = {
            { "Interact", RoseUiAction::Interact },
            { "Chat                 >", RoseUiAction::None, {}, true, false, MenuCategory::Chat },
            { "Projects             >", RoseUiAction::None, {}, true, false, MenuCategory::Projects },
            { "Capabilities         >", RoseUiAction::None, {}, true, false, MenuCategory::Capabilities },
            { "Avatar               >", RoseUiAction::None, {}, true, false, MenuCategory::Avatar },
            { "System               >", RoseUiAction::None, {}, true, false, MenuCategory::System }
        };

        SDL_Window* rawWindow{ nullptr };
        SDL_Renderer* rawRenderer{ nullptr };
        const int height = static_cast<int>(rootEntries_.size()) * rowHeight_ + 8;

        if (!SDL_CreateWindowAndRenderer(
                "Rose Menu",
                width_,
                height,
                SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_HIDDEN,
                &rawWindow,
                &rawRenderer))
        {
            throw std::runtime_error{ std::string{ "Could not create Rose context menu: " } + SDL_GetError() };
        }

        window_.reset(rawWindow);
        renderer_.reset(rawRenderer);

        SDL_Window* rawSubmenuWindow{ nullptr };
        SDL_Renderer* rawSubmenuRenderer{ nullptr };

        if (!SDL_CreateWindowAndRenderer(
                "Rose Submenu",
                submenuWidth_,
                rowHeight_ + 8,
                SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_HIDDEN,
                &rawSubmenuWindow,
                &rawSubmenuRenderer))
        {
            throw std::runtime_error{ std::string{ "Could not create Rose submenu: " } + SDL_GetError() };
        }

        submenuWindow_.reset(rawSubmenuWindow);
        submenuRenderer_.reset(rawSubmenuRenderer);
    }


    SdlRoseContextMenu::~SdlRoseContextMenu() = default;


    void SdlRoseContextMenu::setWorkspaceSnapshot(
        workspace::WorkspaceSnapshot snapshot)
    {
        workspaceSnapshot_ = std::move(snapshot);

        // Rebuild an already-open dynamic submenu so newly created or activated
        // discussions/projects appear without requiring the user to close the menu.
        if (submenuVisible_)
        {
            const MenuCategory category = activeCategory_;
            const int rootIndex = hoveredRootIndex_;
            openSubmenu(category, rootIndex);
        }
    }


    void SdlRoseContextMenu::show(const float globalX, const float globalY)
    {
        rootGlobalX_ = static_cast<int>(globalX);
        rootGlobalY_ = static_cast<int>(globalY);

        SDL_SetWindowPosition(window_.get(), rootGlobalX_, rootGlobalY_);
        SDL_ShowWindow(window_.get());
        SDL_RaiseWindow(window_.get());

        visible_ = true;
        hoveredRootIndex_ = -1;
        hideSubmenu();
    }


    void SdlRoseContextMenu::hide() noexcept
    {
        if (window_)
        {
            SDL_HideWindow(window_.get());
        }

        hideSubmenu();
        visible_ = false;
        hoveredRootIndex_ = -1;
    }


    bool SdlRoseContextMenu::visible() const noexcept
    {
        return visible_;
    }


    std::optional<RoseUiCommand> SdlRoseContextMenu::handleEvent(const SDL_Event& event)
    {
        if (!visible_)
        {
            return std::nullopt;
        }

        const SDL_WindowID rootId = SDL_GetWindowID(window_.get());
        const SDL_WindowID submenuId = SDL_GetWindowID(submenuWindow_.get());

        if (
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
            && (event.window.windowID == rootId || event.window.windowID == submenuId))
        {
            hide();
            return std::nullopt;
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID == rootId)
        {
            hoveredRootIndex_ = rootEntryIndexAt(event.motion.y);
            hoveredSubmenuIndex_ = -1;

            if (hoveredRootIndex_ >= 0)
            {
                const Entry& entry = rootEntries_[static_cast<std::size_t>(hoveredRootIndex_)];
                if (entry.category != MenuCategory::None)
                {
                    openSubmenu(entry.category, hoveredRootIndex_);
                }
                else
                {
                    hideSubmenu();
                }
            }
            else
            {
                hideSubmenu();
            }

            return std::nullopt;
        }

        if (
            event.type == SDL_EVENT_MOUSE_MOTION
            && submenuVisible_
            && event.motion.windowID == submenuId)
        {
            hoveredSubmenuIndex_ = submenuEntryIndexAt(event.motion.y);
            return std::nullopt;
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            const SDL_WindowID eventWindowId = event.button.windowID;
            const bool insideMenu =
                eventWindowId == rootId
                || (submenuVisible_ && eventWindowId == submenuId);

            if (!insideMenu)
            {
                hide();
                return std::nullopt;
            }

            if (event.button.button == SDL_BUTTON_RIGHT)
            {
                hide();
                return std::nullopt;
            }

            if (event.button.button != SDL_BUTTON_LEFT)
            {
                return std::nullopt;
            }

            if (eventWindowId == rootId)
            {
                const int index = rootEntryIndexAt(event.button.y);
                if (index < 0)
                {
                    return std::nullopt;
                }

                const Entry& entry = rootEntries_[static_cast<std::size_t>(index)];

                if (entry.category != MenuCategory::None)
                {
                    hoveredRootIndex_ = index;
                    openSubmenu(entry.category, index);
                    return std::nullopt;
                }

                if (entry.enabled && entry.action != RoseUiAction::None)
                {
                    RoseUiCommand command{
                        .action = entry.action,
                        .targetId = entry.targetId
                    };
                    hide();
                    return command;
                }

                return std::nullopt;
            }

            if (eventWindowId == submenuId && submenuVisible_)
            {
                const int index = submenuEntryIndexAt(event.button.y);
                if (index < 0)
                {
                    return std::nullopt;
                }

                const Entry& entry = submenuEntries_[static_cast<std::size_t>(index)];
                if (entry.enabled && entry.action != RoseUiAction::None)
                {
                    RoseUiCommand command{
                        .action = entry.action,
                        .targetId = entry.targetId
                    };
                    hide();
                    return command;
                }
            }
        }

        return std::nullopt;
    }


    void SdlRoseContextMenu::render()
    {
        if (!visible_)
        {
            return;
        }

        renderEntries(
            renderer_.get(),
            rootEntries_,
            hoveredRootIndex_,
            width_);

        if (submenuVisible_)
        {
            renderEntries(
                submenuRenderer_.get(),
                submenuEntries_,
                hoveredSubmenuIndex_,
                submenuWidth_);
        }
    }


    int SdlRoseContextMenu::rootEntryIndexAt(const float y) const noexcept
    {
        const int index = static_cast<int>((y - 3.0f) / static_cast<float>(rowHeight_));
        if (index < 0 || index >= static_cast<int>(rootEntries_.size()))
        {
            return -1;
        }
        return index;
    }


    int SdlRoseContextMenu::submenuEntryIndexAt(const float y) const noexcept
    {
        const int index = static_cast<int>((y - 3.0f) / static_cast<float>(rowHeight_));
        if (index < 0 || index >= static_cast<int>(submenuEntries_.size()))
        {
            return -1;
        }
        return index;
    }


    std::vector<SdlRoseContextMenu::Entry>
        SdlRoseContextMenu::buildSubmenuEntries(const MenuCategory category) const
    {
        std::vector<Entry> entries;

        const auto findProject =
            [&](const std::string_view projectId)
                -> const workspace::ProjectRecord*
            {
                const auto found = std::ranges::find_if(
                    workspaceSnapshot_.projects,
                    [projectId](const workspace::ProjectRecord& project)
                    {
                        return project.id == projectId;
                    });

                return found == workspaceSnapshot_.projects.end()
                    ? nullptr
                    : &*found;
            };

        const auto discussionVisible =
            [&](const workspace::DiscussionRecord& discussion)
            {
                if (discussion.removed)
                {
                    return false;
                }

                if (!discussion.projectId.has_value())
                {
                    return true;
                }

                const workspace::ProjectRecord* project =
                    findProject(*discussion.projectId);

                return project != nullptr && !project->removed;
            };

        const workspace::DiscussionRecord* activeDiscussion{ nullptr };
        if (workspaceSnapshot_.activeDiscussionId.has_value())
        {
            const auto found = std::ranges::find_if(
                workspaceSnapshot_.discussions,
                [&](const workspace::DiscussionRecord& discussion)
                {
                    return discussion.id == *workspaceSnapshot_.activeDiscussionId;
                });

            if (
                found != workspaceSnapshot_.discussions.end()
                && discussionVisible(*found))
            {
                activeDiscussion = &*found;
            }
        }

        const workspace::ProjectRecord* activeProject{ nullptr };
        if (activeDiscussion != nullptr && activeDiscussion->projectId.has_value())
        {
            const workspace::ProjectRecord* candidate =
                findProject(*activeDiscussion->projectId);

            if (candidate != nullptr && !candidate->removed)
            {
                activeProject = candidate;
            }
        }

        switch (category)
        {
        case MenuCategory::Chat:
        {
            entries.push_back({ "Open current discussion", RoseUiAction::ShowChat });
            entries.push_back({ "New discussion", RoseUiAction::NewChat });
            entries.push_back({
                activeDiscussion != nullptr
                    ? "Remove active discussion"
                    : "Remove active discussion (none)",
                RoseUiAction::RemoveDiscussion,
                activeDiscussion != nullptr ? activeDiscussion->id : std::string{},
                activeDiscussion != nullptr
            });
            entries.push_back({ "DISCUSSIONS", RoseUiAction::None, {}, false, true });

            std::vector<const workspace::DiscussionRecord*> discussions;
            discussions.reserve(workspaceSnapshot_.discussions.size());
            for (const workspace::DiscussionRecord& discussion : workspaceSnapshot_.discussions)
            {
                if (discussionVisible(discussion))
                {
                    discussions.push_back(&discussion);
                }
            }

            std::ranges::sort(
                discussions,
                [](const workspace::DiscussionRecord* left, const workspace::DiscussionRecord* right)
                {
                    return left->updatedUnixMilliseconds > right->updatedUnixMilliseconds;
                });

            const std::size_t count = std::min(maximumRecentEntries, discussions.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                const workspace::DiscussionRecord& discussion = *discussions[i];
                const bool active =
                    workspaceSnapshot_.activeDiscussionId.has_value()
                    && *workspaceSnapshot_.activeDiscussionId == discussion.id;

                entries.push_back({
                    std::string{ active ? "* " : "  " }
                        + clippedLabel(discussion.title, 37),
                    RoseUiAction::ActivateDiscussion,
                    discussion.id
                });
            }

            if (count == 0)
            {
                entries.push_back({ "  (No discussions yet)", RoseUiAction::None, {}, false });
            }

            std::vector<const workspace::DiscussionRecord*> removedDiscussions;
            for (const workspace::DiscussionRecord& discussion : workspaceSnapshot_.discussions)
            {
                if (!discussion.removed)
                {
                    continue;
                }

                // A discussion whose whole project is removed is restored by
                // restoring that project. Do not duplicate it in both sections.
                if (discussion.projectId.has_value())
                {
                    const workspace::ProjectRecord* project =
                        findProject(*discussion.projectId);
                    if (project != nullptr && project->removed)
                    {
                        continue;
                    }
                }

                removedDiscussions.push_back(&discussion);
            }

            std::ranges::sort(
                removedDiscussions,
                [](const workspace::DiscussionRecord* left, const workspace::DiscussionRecord* right)
                {
                    return left->updatedUnixMilliseconds > right->updatedUnixMilliseconds;
                });

            if (!removedDiscussions.empty())
            {
                entries.push_back({ "REMOVED DISCUSSIONS", RoseUiAction::None, {}, false, true });
                const std::size_t removedCount =
                    std::min(maximumRecentEntries, removedDiscussions.size());
                for (std::size_t i = 0; i < removedCount; ++i)
                {
                    const workspace::DiscussionRecord& discussion = *removedDiscussions[i];
                    entries.push_back({
                        "Restore: " + clippedLabel(discussion.title, 31),
                        RoseUiAction::RestoreDiscussion,
                        discussion.id
                    });
                }
            }

            entries.push_back({ "Manage discussions", RoseUiAction::ManageDiscussions });
            break;
        }

        case MenuCategory::Projects:
        {
            entries.push_back({ "New project", RoseUiAction::NewProject });
            entries.push_back({
                activeProject != nullptr
                    ? "Remove active project"
                    : "Remove active project (none)",
                RoseUiAction::RemoveProject,
                activeProject != nullptr ? activeProject->id : std::string{},
                activeProject != nullptr
            });
            entries.push_back({ "PROJECTS", RoseUiAction::None, {}, false, true });

            std::vector<const workspace::ProjectRecord*> projects;
            projects.reserve(workspaceSnapshot_.projects.size());
            for (const workspace::ProjectRecord& project : workspaceSnapshot_.projects)
            {
                if (!project.removed)
                {
                    projects.push_back(&project);
                }
            }

            std::ranges::sort(
                projects,
                [](const workspace::ProjectRecord* left, const workspace::ProjectRecord* right)
                {
                    return left->updatedUnixMilliseconds > right->updatedUnixMilliseconds;
                });

            const std::size_t count = std::min(maximumRecentEntries, projects.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                const workspace::ProjectRecord& project = *projects[i];
                const bool active = activeProject != nullptr && activeProject->id == project.id;
                entries.push_back({
                    std::string{ active ? "* " : "  " }
                        + clippedLabel(project.title, 37),
                    RoseUiAction::OpenProject,
                    project.id
                });
            }

            if (count == 0)
            {
                entries.push_back({ "  (No projects yet)", RoseUiAction::None, {}, false });
            }

            std::vector<const workspace::ProjectRecord*> removedProjects;
            for (const workspace::ProjectRecord& project : workspaceSnapshot_.projects)
            {
                if (project.removed)
                {
                    removedProjects.push_back(&project);
                }
            }

            std::ranges::sort(
                removedProjects,
                [](const workspace::ProjectRecord* left, const workspace::ProjectRecord* right)
                {
                    return left->updatedUnixMilliseconds > right->updatedUnixMilliseconds;
                });

            if (!removedProjects.empty())
            {
                entries.push_back({ "REMOVED PROJECTS", RoseUiAction::None, {}, false, true });
                const std::size_t removedCount =
                    std::min(maximumRecentEntries, removedProjects.size());
                for (std::size_t i = 0; i < removedCount; ++i)
                {
                    const workspace::ProjectRecord& project = *removedProjects[i];
                    entries.push_back({
                        "Restore: " + clippedLabel(project.title, 31),
                        RoseUiAction::RestoreProject,
                        project.id
                    });
                }
            }

            entries.push_back({ "Manage projects", RoseUiAction::ManageProjects });
            break;
        }

        case MenuCategory::Capabilities:
            entries = {
                { "New reminder", RoseUiAction::NewErrand },
                { "Errands / reminders", RoseUiAction::Errands },
                { "Filesystem management", RoseUiAction::Files },
                { "Project knowledge", RoseUiAction::ProjectKnowledge },
                { "Memory management", RoseUiAction::Memory },
                { "Integrations / permissions", RoseUiAction::Integrations },
                { "Outlook mail", RoseUiAction::Email },
                { "Search online (coming soon)", RoseUiAction::SearchOnline, {}, false },
                { "Search offline", RoseUiAction::SearchOffline },
                { "Analyze current screen", RoseUiAction::AnalyzeScreen }
            };
            break;

        case MenuCategory::Avatar:
            entries = {
                { "Demonstrate animations", RoseUiAction::AnimationDemo },
                { "PREVIEW STATE", RoseUiAction::None, {}, false, true },
                { "  Idle", RoseUiAction::PreviewIdle },
                { "  Listening", RoseUiAction::PreviewListening },
                { "  Thinking", RoseUiAction::PreviewThinking },
                { "  Speaking", RoseUiAction::PreviewSpeaking },
                { "  Working", RoseUiAction::PreviewWorking },
                { "  Notification", RoseUiAction::PreviewNotification },
                { "  Confused", RoseUiAction::PreviewConfused }
            };
            break;

        case MenuCategory::System:
            entries = {
                { "Settings (coming soon)", RoseUiAction::Settings, {}, false },
                { "Exit Rose", RoseUiAction::ExitRose }
            };
            break;

        case MenuCategory::None:
            break;
        }

        return entries;
    }


    void SdlRoseContextMenu::openSubmenu(
        const MenuCategory category,
        const int rootIndex)
    {
        if (category == MenuCategory::None || rootIndex < 0)
        {
            hideSubmenu();
            return;
        }

        activeCategory_ = category;
        submenuEntries_ = buildSubmenuEntries(category);
        hoveredSubmenuIndex_ = -1;

        if (submenuEntries_.empty())
        {
            hideSubmenu();
            return;
        }

        const int height = static_cast<int>(submenuEntries_.size()) * rowHeight_ + 8;
        SDL_SetWindowSize(submenuWindow_.get(), submenuWidth_, height);
        SDL_SetWindowPosition(
            submenuWindow_.get(),
            rootGlobalX_ + width_ - 2,
            rootGlobalY_ + 3 + (rootIndex * rowHeight_));
        SDL_ShowWindow(submenuWindow_.get());
        SDL_RaiseWindow(submenuWindow_.get());
        submenuVisible_ = true;
    }


    void SdlRoseContextMenu::hideSubmenu() noexcept
    {
        if (submenuWindow_)
        {
            SDL_HideWindow(submenuWindow_.get());
        }

        submenuVisible_ = false;
        hoveredSubmenuIndex_ = -1;
        activeCategory_ = MenuCategory::None;
        submenuEntries_.clear();
    }


    void SdlRoseContextMenu::renderEntries(
        SDL_Renderer* renderer,
        const std::vector<Entry>& entries,
        const int hoveredIndex,
        const int width)
    {
        SDL_SetRenderDrawColor(renderer, 24, 22, 30, 248);
        SDL_RenderClear(renderer);

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const Entry& entry = entries[i];
            const float y = 5.0f + static_cast<float>(i * rowHeight_);

            if (static_cast<int>(i) == hoveredIndex && entry.enabled)
            {
                SDL_FRect highlight{ 3.0f, y - 2.0f, static_cast<float>(width - 6), static_cast<float>(rowHeight_) };
                SDL_SetRenderDrawColor(renderer, 68, 61, 83, 255);
                SDL_RenderFillRect(renderer, &highlight);
            }

            if (entry.heading)
            {
                SDL_SetRenderDrawColor(renderer, 151, 139, 178, 255);
            }
            else if (entry.enabled)
            {
                SDL_SetRenderDrawColor(renderer, 238, 233, 246, 255);
            }
            else
            {
                SDL_SetRenderDrawColor(renderer, 113, 108, 122, 255);
            }

            SDL_RenderDebugText(renderer, 8.0f, y, entry.label.c_str());
        }

        SDL_RenderPresent(renderer);
    }


    std::string SdlRoseContextMenu::clippedLabel(
        const std::string_view text,
        const std::size_t maximumCharacters)
    {
        if (text.size() <= maximumCharacters)
        {
            return std::string{ text };
        }

        if (maximumCharacters <= 3)
        {
            return std::string{ text.substr(0, maximumCharacters) };
        }

        return std::string{ text.substr(0, maximumCharacters - 3) } + "...";
    }


    void SdlRoseContextMenu::WindowDeleter::operator()(SDL_Window* value) const noexcept
    {
        if (value) SDL_DestroyWindow(value);
    }


    void SdlRoseContextMenu::RendererDeleter::operator()(SDL_Renderer* value) const noexcept
    {
        if (value) SDL_DestroyRenderer(value);
    }
}
