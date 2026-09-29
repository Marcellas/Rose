#pragma once

#include <cstdint>
#include <string>

namespace rose::ui
{
    enum class RoseUiAction : std::uint8_t
    {
        None,
        Interact,
        ShowChat,
        NewChat,
        ActivateDiscussion,
        RemoveDiscussion,
        RestoreDiscussion,
        RecentChats,
        ManageDiscussions,
        NewProject,
        OpenProject,
        RemoveProject,
        RestoreProject,
        ManageProjects,
        Email,
        SearchOnline,
        SearchOffline,
        AnalyzeScreen,
        Files,
        ProjectKnowledge,
        Memory,
        Errands,
        NewErrand,
        Integrations,
        AnimationDemo,
        PreviewIdle,
        PreviewListening,
        PreviewThinking,
        PreviewSpeaking,
        PreviewWorking,
        PreviewNotification,
        PreviewConfused,
        Settings,
        ExitRose
    };


    // One desktop-menu action plus optional durable workspace identity.
    // Static actions leave targetId empty; dynamic Discussion/Project rows carry
    // the persisted id rather than a pointer/reference into a transient UI copy.
    struct RoseUiCommand
    {
        RoseUiAction action{ RoseUiAction::None };
        std::string targetId;
    };
}
