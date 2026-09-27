#include "persistence/DiscussionConversationStore.h"
#include "workspace/FileWorkspaceStore.h"
#include "workspace/WorkspaceRepository.h"
#include "workspace/WorkspaceCommand.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void require(
        const bool condition,
        const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }


    std::filesystem::path makeTemporaryRoot()
    {
        std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / "rose_workspace_system_test";

        std::error_code error;
        std::filesystem::remove_all(root, error);
        error.clear();
        std::filesystem::create_directories(root, error);

        if (error)
        {
            throw std::runtime_error{
                "Could not create temporary Rose workspace test directory."
            };
        }

        return root;
    }
}


int main()
{
    try
    {
        const std::filesystem::path root =
            makeTemporaryRoot();

        const std::filesystem::path workspacePath =
            root / "workspace.rosews";

        const std::filesystem::path discussionDirectory =
            root / "discussions";

        rose::workspace::ProjectId projectId;
        rose::workspace::DiscussionId projectDiscussionId;
        rose::workspace::DiscussionId looseDiscussionId;

        // -----------------------------------------------------------------
        // Create and mutate one workspace.
        // -----------------------------------------------------------------
        {
            rose::workspace::FileWorkspaceStore store{
                workspacePath
            };

            rose::workspace::WorkspaceRepository repository{
                store
            };

            require(
                repository.snapshot().projects.empty(),
                "First launch should contain no projects.");

            require(
                repository.snapshot().discussions.empty(),
                "First launch should contain no discussions.");

            projectId =
                repository.createProject(
                    "Rose Development");

            repository.setProjectInstructions(
                projectId,
                "Prefer readable C++20 and local-first behavior.");

            repository.addProjectApprovedRoot(
                projectId,
                "C:/Users/chris/OneDrive/Desktop/Rose");

            // Duplicate roots should be ignored instead of persisted twice.
            repository.addProjectApprovedRoot(
                projectId,
                "C:/Users/chris/OneDrive/Desktop/Rose");

            repository.addProjectApprovedRoot(
                projectId,
                "C:/Users/chris/OneDrive/Desktop/Rose/tmp");

            require(
                repository.removeProjectApprovedRoot(
                    projectId,
                    "C:/Users/chris/OneDrive/Desktop/Rose/tmp"),
                "Added approved root should be removable.");

            require(
                !repository.removeProjectApprovedRoot(
                    projectId,
                    "C:/Users/chris/OneDrive/Desktop/Rose/tmp"),
                "Removing the same approved root twice should report false.");

            repository.addProjectIndexedDocument(
                projectId,
                "README.md");

            repository.addProjectIndexedDocument(
                projectId,
                "temporary.txt");
            require(
                repository.removeProjectIndexedDocument(
                    projectId,
                    "temporary.txt"),
                "Added indexed document should be removable.");

            repository.addProjectMemoryRecord(
                projectId,
                42);

            // Duplicate memory membership is ignored.
            repository.addProjectMemoryRecord(
                projectId,
                42);

            repository.setProjectSetting(
                projectId,
                "response_style",
                "teaching");

            // Setting the same key updates instead of duplicating it.
            repository.setProjectSetting(
                projectId,
                "response_style",
                "collaborative");

            repository.setProjectEnabledTools(
                projectId,
                {
                    "read_text_file",
                    "scan_directory_tree",
                    "read_text_file"
                });

            projectDiscussionId =
                repository.createDiscussion(
                    "Batch 20 implementation",
                    projectId);

            repository.setDiscussionTags(
                projectDiscussionId,
                { "rose", "coding", "rose" });

            looseDiscussionId =
                repository.createDiscussion(
                    "Unassigned scratch discussion");

            // Creating each discussion opens it, so both can remain open while
            // the newest one becomes active.
            require(
                repository.snapshot().openDiscussionIds.size() == 2,
                "Two created discussions should both remain open.");

            require(
                repository.snapshot().activeDiscussionId
                    == looseDiscussionId,
                "Newest discussion should become active.");

            repository.activateDiscussion(
                projectDiscussionId);

            repository.renameDiscussion(
                projectDiscussionId,
                "Batch 20 workspace persistence");

            repository.closeDiscussion(
                looseDiscussionId);

            require(
                repository.snapshot().activeDiscussionId
                    == projectDiscussionId,
                "Closing an inactive discussion must not change the active one.");

            require(
                repository.removeDiscussion(looseDiscussionId),
                "A visible discussion should be removable.");
            require(
                repository.findDiscussion(looseDiscussionId)->removed,
                "Removed discussion should keep its durable record.");
            require(
                repository.restoreDiscussion(looseDiscussionId),
                "Removed discussion should be restorable.");

            require(
                repository.removeProject(projectId),
                "A visible project should be removable.");
            require(
                repository.findProject(projectId)->removed,
                "Removed project should keep its durable record.");
            require(
                !repository.snapshot().activeDiscussionId.has_value(),
                "Removing the active project should clear an unavailable active discussion.");
            require(
                repository.restoreProject(projectId),
                "Removed project should be restorable.");
            repository.activateDiscussion(projectDiscussionId);
        }

        // -----------------------------------------------------------------
        // Reload from disk and verify durable metadata.
        // -----------------------------------------------------------------
        {
            rose::workspace::FileWorkspaceStore store{
                workspacePath
            };

            rose::workspace::WorkspaceRepository repository{
                store
            };

            require(
                repository.snapshot().projects.size() == 1,
                "Persisted project count mismatch.");

            require(
                repository.snapshot().discussions.size() == 2,
                "Persisted discussion count mismatch.");

            const auto* project =
                repository.findProject(projectId);

            require(project != nullptr, "Persisted project was not found.");
            require(
                project->instructions
                    == "Prefer readable C++20 and local-first behavior.",
                "Project instructions were not persisted.");
            require(
                project->approvedFilesystemRoots.size() == 1,
                "Duplicate approved roots should not be persisted.");
            require(
                project->indexedDocuments.size() == 1,
                "Indexed documents were not persisted.");
            require(
                project->memoryRecordIds.size() == 1
                    && project->memoryRecordIds.front() == 42,
                "Project memory membership was not persisted.");
            require(
                project->settings.size() == 1
                    && project->settings.front().key == "response_style"
                    && project->settings.front().value == "collaborative",
                "Project settings were not persisted or updated correctly.");
            require(
                project->enabledTools.size() == 2,
                "Duplicate enabled tools should be removed.");

            const auto* discussion =
                repository.findDiscussion(projectDiscussionId);

            require(
                discussion != nullptr,
                "Persisted project discussion was not found.");
            require(
                discussion->title
                    == "Batch 20 workspace persistence",
                "Renamed discussion title was not persisted.");
            require(
                discussion->projectId == projectId,
                "Discussion/project ownership was not persisted.");
            require(
                discussion->tags.size() == 2,
                "Duplicate discussion tags should be removed.");

            require(
                repository.snapshot().openDiscussionIds.size() == 1,
                "Closed discussion should remain closed after reload.");
            require(
                repository.snapshot().activeDiscussionId
                    == projectDiscussionId,
                "Active discussion was not restored.");

            require(
                repository.discussionsForProject(projectId).size() == 1,
                "Project discussion lookup returned the wrong count.");
        }

        // -----------------------------------------------------------------
        // Verify soft-removal state itself survives restart and remains reversible.
        // -----------------------------------------------------------------
        {
            rose::workspace::FileWorkspaceStore store{ workspacePath };
            rose::workspace::WorkspaceRepository repository{ store };

            require(
                repository.removeDiscussion(looseDiscussionId),
                "Discussion removal should change persisted workspace state.");
            require(
                repository.removeProject(projectId),
                "Project removal should change persisted workspace state.");
        }

        {
            rose::workspace::FileWorkspaceStore store{ workspacePath };
            rose::workspace::WorkspaceRepository repository{ store };

            require(
                repository.findDiscussion(looseDiscussionId)->removed,
                "Removed discussion flag was not restored from disk.");
            require(
                repository.findProject(projectId)->removed,
                "Removed project flag was not restored from disk.");
            require(
                !repository.discussionVisible(projectDiscussionId),
                "Discussion inside a removed project must be hidden.");

            require(
                repository.restoreProject(projectId),
                "Persisted project removal should be reversible.");
            require(
                repository.restoreDiscussion(looseDiscussionId),
                "Persisted discussion removal should be reversible.");
            repository.activateDiscussion(projectDiscussionId);
        }

        // -----------------------------------------------------------------
        // Verify workspace slash-command parsing.
        // -----------------------------------------------------------------
        {
            using rose::workspace::WorkspaceCommandKind;

            const auto removeActive =
                rose::workspace::parseWorkspaceCommand("/discussion remove");
            require(
                removeActive.has_value()
                    && removeActive->kind == WorkspaceCommandKind::RemoveDiscussion
                    && removeActive->targetId.empty(),
                "Active discussion remove command did not parse.");

            const auto restoreProject =
                rose::workspace::parseWorkspaceCommand("/project restore p-123");
            require(
                restoreProject.has_value()
                    && restoreProject->kind == WorkspaceCommandKind::RestoreProject
                    && restoreProject->targetId == "p-123",
                "Project restore command did not parse.");

            require(
                !rose::workspace::parseWorkspaceCommand("normal chat text").has_value(),
                "Normal chat text must not parse as a workspace command.");
        }

        // -----------------------------------------------------------------
        // Verify each discussion owns an isolated transcript journal.
        // -----------------------------------------------------------------
        {
            rose::persistence::DiscussionConversationStore conversations{
                discussionDirectory
            };

            require(
                conversations.loadTurns().empty(),
                "No active discussion should load an empty transcript.");

            conversations.selectDiscussion(projectDiscussionId);
            conversations.appendTurn(
                "What are we implementing?",
                "Persistent projects and discussions.");

            conversations.selectDiscussion(looseDiscussionId);
            conversations.appendTurn(
                "Scratch note",
                "Independent transcript.");

            auto looseTurns =
                conversations.loadTurns();

            require(
                looseTurns.size() == 1,
                "Loose discussion transcript count mismatch.");
            require(
                looseTurns.front().userText == "Scratch note",
                "Loose discussion transcript contains another discussion's turn.");

            conversations.selectDiscussion(projectDiscussionId);

            auto projectTurns =
                conversations.loadTurns();

            require(
                projectTurns.size() == 1,
                "Project discussion transcript count mismatch.");
            require(
                projectTurns.front().assistantText
                    == "Persistent projects and discussions.",
                "Project discussion transcript was not isolated.");
        }

        std::error_code cleanupError;
        std::filesystem::remove_all(root, cleanupError);

        std::cout
            << "Rose WorkspaceSystem tests: PASS\n";

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Rose WorkspaceSystem tests: FAIL: "
            << exception.what()
            << '\n';

        return 1;
    }
}
