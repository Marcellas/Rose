#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rose::workspace
{

    // Stable persisted identifiers. They are strings on purpose: the storage
    // layer and future UI should not need to care how Rose generates them.
    using ProjectId = std::string;
    using DiscussionId = std::string;


    struct ProjectSetting
    {
        std::string key;
        std::string value;
    };


    // A Project is durable organizational context around one or more discussions.
    // Model-provider state does NOT live here; changing providers must not change
    // project identity, instructions, approved roots, or indexed source choices.
    struct ProjectRecord
    {
        ProjectId id;
        std::string title;
        std::string instructions;

        // Paths are stored as UTF-8 strings instead of filesystem::path so the
        // persisted model is provider/platform neutral. Platform adapters may
        // translate these to native paths when tools actually execute.
        std::vector<std::string> approvedFilesystemRoots;
        std::vector<std::string> indexedDocuments;

        // Durable memory records remain owned by MemoryRepository. A project
        // stores only the ids it has explicitly adopted into its context scope.
        std::vector<std::uint64_t> memoryRecordIds;

        // Tool ids are registry ids such as "read_text_file". This records a
        // project's desired tool surface; PermissionSystem remains authoritative.
        std::vector<std::string> enabledTools;

        // Small provider-neutral project preferences. These are intentionally
        // key/value metadata rather than arbitrary executable configuration.
        std::vector<ProjectSetting> settings;

        std::int64_t createdUnixMilliseconds{ 0 };
        std::int64_t updatedUnixMilliseconds{ 0 };

        // Batch 27: Remove is intentionally a reversible workspace operation.
        // Removed projects are hidden from normal UI/retrieval selection while
        // retaining their metadata, discussions, knowledge roots and provenance.
        bool removed{ false };
    };


    // A Discussion is Rose's durable identity for a chat/thread.
    // Transcript bytes remain in IConversationStore implementations; this record
    // contains only lightweight searchable/organizational metadata.
    struct DiscussionRecord
    {
        DiscussionId id;
        std::string title;
        std::optional<ProjectId> projectId;
        std::vector<std::string> tags;

        std::int64_t createdUnixMilliseconds{ 0 };
        std::int64_t updatedUnixMilliseconds{ 0 };

        // Soft removal keeps the transcript file intact. A removed discussion can
        // therefore be restored without reconstructing conversation history.
        bool removed{ false };
    };


    // Entire lightweight desktop-workspace snapshot.
    //
    // openDiscussionIds allows the future UI to restore several open tabs/windows
    // without forcing all of their transcript text into the active model prompt.
    struct WorkspaceSnapshot
    {
        std::vector<ProjectRecord> projects;
        std::vector<DiscussionRecord> discussions;
        std::vector<DiscussionId> openDiscussionIds;
        std::optional<DiscussionId> activeDiscussionId;
    };

} // namespace rose::workspace
