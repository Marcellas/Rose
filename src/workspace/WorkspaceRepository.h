#pragma once

#include "workspace/IWorkspaceStore.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rose::workspace
{

    // Owns the in-memory Project/Discussion catalog and all invariants around it.
    //
    // Lifetime/ownership:
    //   - IWorkspaceStore is borrowed and must outlive this repository.
    //   - Records are owned by snapshot_ inside the repository.
    //   - Returned references remain valid only until the next mutation that may
    //     grow the corresponding vector; callers should prefer ids for durable UI
    //     state rather than holding pointers to records.
    class WorkspaceRepository final
    {
    public:
        explicit WorkspaceRepository(
            IWorkspaceStore& store);

        [[nodiscard]]
        const WorkspaceSnapshot& snapshot() const noexcept;

        [[nodiscard]]
        const ProjectRecord* findProject(
            std::string_view projectId) const noexcept;

        [[nodiscard]]
        const DiscussionRecord* findDiscussion(
            std::string_view discussionId) const noexcept;

        [[nodiscard]]
        ProjectId createProject(
            std::string title);

        [[nodiscard]]
        DiscussionId createDiscussion(
            std::string title,
            std::optional<ProjectId> projectId = std::nullopt);

        // "Remove" is a reversible workspace operation. Records and transcript
        // files remain intact; normal lists simply stop surfacing removed items.
        [[nodiscard]]
        bool removeProject(std::string_view projectId);

        [[nodiscard]]
        bool restoreProject(std::string_view projectId);

        [[nodiscard]]
        bool removeDiscussion(std::string_view discussionId);

        [[nodiscard]]
        bool restoreDiscussion(std::string_view discussionId);

        void renameProject(
            std::string_view projectId,
            std::string title);

        void renameDiscussion(
            std::string_view discussionId,
            std::string title);

        void setProjectInstructions(
            std::string_view projectId,
            std::string instructions);

        void setDiscussionProject(
            std::string_view discussionId,
            std::optional<ProjectId> projectId);

        void setDiscussionTags(
            std::string_view discussionId,
            std::vector<std::string> tags);

        void addProjectApprovedRoot(
            std::string_view projectId,
            std::string root);

        [[nodiscard]]
        bool removeProjectApprovedRoot(
            std::string_view projectId,
            std::string_view root);

        void addProjectIndexedDocument(
            std::string_view projectId,
            std::string document);

        [[nodiscard]]
        bool removeProjectIndexedDocument(
            std::string_view projectId,
            std::string_view document);

        void addProjectMemoryRecord(
            std::string_view projectId,
            std::uint64_t memoryRecordId);

        void removeProjectMemoryRecord(
            std::string_view projectId,
            std::uint64_t memoryRecordId);

        void setProjectEnabledTools(
            std::string_view projectId,
            std::vector<std::string> toolIds);

        void setProjectSetting(
            std::string_view projectId,
            std::string key,
            std::string value);

        // Open/active UI state is persisted independently from transcript text.
        // Several discussions may be open, but only one may be active at a time.
        void openDiscussion(
            std::string_view discussionId);

        void closeDiscussion(
            std::string_view discussionId);

        void activateDiscussion(
            std::string_view discussionId);

        [[nodiscard]]
        std::vector<const DiscussionRecord*> discussionsForProject(
            std::string_view projectId) const;

        [[nodiscard]]
        bool discussionVisible(std::string_view discussionId) const noexcept;

    private:
        [[nodiscard]]
        ProjectRecord& requireProject(
            std::string_view projectId);

        [[nodiscard]]
        DiscussionRecord& requireDiscussion(
            std::string_view discussionId);

        [[nodiscard]]
        ProjectId generateProjectId();

        [[nodiscard]]
        DiscussionId generateDiscussionId();

        void validateLoadedSnapshot() const;
        void persist();

        IWorkspaceStore& store_;
        WorkspaceSnapshot snapshot_;
        std::uint64_t nextIdSequence_{ 1 };
    };

} // namespace rose::workspace
