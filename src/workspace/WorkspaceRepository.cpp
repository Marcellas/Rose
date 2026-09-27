#include "workspace/WorkspaceRepository.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace rose::workspace
{

    namespace
    {
        std::int64_t currentUnixMilliseconds()
        {
            const auto now =
                std::chrono::system_clock::now();

            return std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
        }


        bool isBlank(
            const std::string_view text)
        {
            return std::all_of(
                text.begin(),
                text.end(),
                [](const unsigned char value)
                {
                    return std::isspace(value) != 0;
                });
        }


        void requireNonBlank(
            const std::string_view text,
            const std::string_view what)
        {
            if (text.empty() || isBlank(text))
            {
                throw std::invalid_argument{
                    std::string{ what }
                    + " cannot be empty."
                };
            }
        }


        void deduplicateNonBlank(
            std::vector<std::string>& values,
            const std::string_view what)
        {
            std::unordered_set<std::string> seen;
            std::vector<std::string> clean;
            clean.reserve(values.size());

            for (std::string& value : values)
            {
                requireNonBlank(value, what);

                if (seen.insert(value).second)
                {
                    clean.push_back(std::move(value));
                }
            }

            values = std::move(clean);
        }


        std::string makeId(
            const char prefix,
            const std::uint64_t sequence)
        {
            std::ostringstream stream;

            stream
                << prefix
                << '-'
                << std::hex
                << static_cast<std::uint64_t>(
                    currentUnixMilliseconds())
                << '-'
                << sequence;

            return stream.str();
        }
    } // namespace


    WorkspaceRepository::WorkspaceRepository(
        IWorkspaceStore& store)
        : store_{ store }
        , snapshot_{ store_.load() }
    {
        validateLoadedSnapshot();
    }


    const WorkspaceSnapshot&
        WorkspaceRepository::snapshot() const noexcept
    {
        return snapshot_;
    }


    const ProjectRecord* WorkspaceRepository::findProject(
        const std::string_view projectId) const noexcept
    {
        const auto iterator =
            std::find_if(
                snapshot_.projects.begin(),
                snapshot_.projects.end(),
                [projectId](const ProjectRecord& project)
                {
                    return project.id == projectId;
                });

        return iterator == snapshot_.projects.end()
            ? nullptr
            : &*iterator;
    }


    const DiscussionRecord* WorkspaceRepository::findDiscussion(
        const std::string_view discussionId) const noexcept
    {
        const auto iterator =
            std::find_if(
                snapshot_.discussions.begin(),
                snapshot_.discussions.end(),
                [discussionId](const DiscussionRecord& discussion)
                {
                    return discussion.id == discussionId;
                });

        return iterator == snapshot_.discussions.end()
            ? nullptr
            : &*iterator;
    }


    ProjectId WorkspaceRepository::createProject(
        std::string title)
    {
        requireNonBlank(title, "Project title");

        const std::int64_t now =
            currentUnixMilliseconds();

        ProjectRecord project{
            .id = generateProjectId(),
            .title = std::move(title),
            .instructions = {},
            .approvedFilesystemRoots = {},
            .indexedDocuments = {},
            .memoryRecordIds = {},
            .enabledTools = {},
            .settings = {},
            .createdUnixMilliseconds = now,
            .updatedUnixMilliseconds = now,
            .removed = false
        };

        const ProjectId id = project.id;
        snapshot_.projects.push_back(std::move(project));
        persist();

        return id;
    }


    DiscussionId WorkspaceRepository::createDiscussion(
        std::string title,
        std::optional<ProjectId> projectId)
    {
        requireNonBlank(title, "Discussion title");

        if (projectId.has_value())
        {
            const ProjectRecord& project = requireProject(*projectId);
            if (project.removed)
            {
                throw std::logic_error{
                    "Cannot create a discussion inside a removed Rose project."
                };
            }
        }

        const std::int64_t now =
            currentUnixMilliseconds();

        DiscussionRecord discussion{
            .id = generateDiscussionId(),
            .title = std::move(title),
            .projectId = std::move(projectId),
            .tags = {},
            .createdUnixMilliseconds = now,
            .updatedUnixMilliseconds = now,
            .removed = false
        };

        const DiscussionId id = discussion.id;
        snapshot_.discussions.push_back(std::move(discussion));

        // A newly created discussion is immediately visible and active. This is
        // metadata only; the UI decides whether that means a tab, panel, or window.
        snapshot_.openDiscussionIds.push_back(id);
        snapshot_.activeDiscussionId = id;

        persist();
        return id;
    }


    bool WorkspaceRepository::removeProject(
        const std::string_view projectId)
    {
        ProjectRecord& project = requireProject(projectId);
        if (project.removed)
        {
            return false;
        }

        project.removed = true;
        project.updatedUnixMilliseconds = currentUnixMilliseconds();

        // A removed project is no longer eligible to own an open/active chat,
        // but the DiscussionRecord objects themselves remain untouched so a
        // later restore brings the whole project context back.
        std::erase_if(
            snapshot_.openDiscussionIds,
            [&](const DiscussionId& discussionId)
            {
                const DiscussionRecord* discussion = findDiscussion(discussionId);
                return discussion != nullptr
                    && discussion->projectId.has_value()
                    && *discussion->projectId == projectId;
            });

        if (
            snapshot_.activeDiscussionId.has_value()
            && !discussionVisible(*snapshot_.activeDiscussionId))
        {
            if (snapshot_.openDiscussionIds.empty())
            {
                snapshot_.activeDiscussionId.reset();
            }
            else
            {
                snapshot_.activeDiscussionId = snapshot_.openDiscussionIds.back();
            }
        }

        persist();
        return true;
    }


    bool WorkspaceRepository::restoreProject(
        const std::string_view projectId)
    {
        ProjectRecord& project = requireProject(projectId);
        if (!project.removed)
        {
            return false;
        }

        project.removed = false;
        project.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
        return true;
    }


    bool WorkspaceRepository::removeDiscussion(
        const std::string_view discussionId)
    {
        DiscussionRecord& discussion = requireDiscussion(discussionId);
        if (discussion.removed)
        {
            return false;
        }

        discussion.removed = true;
        discussion.updatedUnixMilliseconds = currentUnixMilliseconds();

        std::erase(snapshot_.openDiscussionIds, std::string{ discussionId });

        if (
            snapshot_.activeDiscussionId.has_value()
            && *snapshot_.activeDiscussionId == discussionId)
        {
            if (snapshot_.openDiscussionIds.empty())
            {
                snapshot_.activeDiscussionId.reset();
            }
            else
            {
                snapshot_.activeDiscussionId = snapshot_.openDiscussionIds.back();
            }
        }

        persist();
        return true;
    }


    bool WorkspaceRepository::restoreDiscussion(
        const std::string_view discussionId)
    {
        DiscussionRecord& discussion = requireDiscussion(discussionId);
        if (!discussion.removed)
        {
            return false;
        }

        discussion.removed = false;
        discussion.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
        return true;
    }


    void WorkspaceRepository::renameProject(
        const std::string_view projectId,
        std::string title)
    {
        requireNonBlank(title, "Project title");

        ProjectRecord& project =
            requireProject(projectId);

        project.title = std::move(title);
        project.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
    }


    void WorkspaceRepository::renameDiscussion(
        const std::string_view discussionId,
        std::string title)
    {
        requireNonBlank(title, "Discussion title");

        DiscussionRecord& discussion =
            requireDiscussion(discussionId);

        discussion.title = std::move(title);
        discussion.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
    }


    void WorkspaceRepository::setProjectInstructions(
        const std::string_view projectId,
        std::string instructions)
    {
        ProjectRecord& project =
            requireProject(projectId);

        project.instructions = std::move(instructions);
        project.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
    }


    void WorkspaceRepository::setDiscussionProject(
        const std::string_view discussionId,
        std::optional<ProjectId> projectId)
    {
        if (projectId.has_value())
        {
            const ProjectRecord& project = requireProject(*projectId);
            if (project.removed)
            {
                throw std::logic_error{
                    "Cannot assign a discussion to a removed Rose project."
                };
            }
        }

        DiscussionRecord& discussion =
            requireDiscussion(discussionId);

        discussion.projectId = std::move(projectId);
        discussion.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
    }


    void WorkspaceRepository::setDiscussionTags(
        const std::string_view discussionId,
        std::vector<std::string> tags)
    {
        deduplicateNonBlank(tags, "Discussion tag");

        DiscussionRecord& discussion =
            requireDiscussion(discussionId);

        discussion.tags = std::move(tags);
        discussion.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
    }


    void WorkspaceRepository::addProjectApprovedRoot(
        const std::string_view projectId,
        std::string root)
    {
        requireNonBlank(root, "Approved filesystem root");

        ProjectRecord& project =
            requireProject(projectId);

        const auto existing =
            std::find(
                project.approvedFilesystemRoots.begin(),
                project.approvedFilesystemRoots.end(),
                root);

        if (existing == project.approvedFilesystemRoots.end())
        {
            project.approvedFilesystemRoots.push_back(std::move(root));
            project.updatedUnixMilliseconds = currentUnixMilliseconds();
            persist();
        }
    }


    bool WorkspaceRepository::removeProjectApprovedRoot(
        const std::string_view projectId,
        const std::string_view root)
    {
        requireNonBlank(root, "Approved filesystem root");

        ProjectRecord& project =
            requireProject(projectId);

        const auto oldSize =
            project.approvedFilesystemRoots.size();

        std::erase(
            project.approvedFilesystemRoots,
            std::string{ root });

        if (project.approvedFilesystemRoots.size() == oldSize)
        {
            return false;
        }

        project.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
        return true;
    }


    void WorkspaceRepository::addProjectIndexedDocument(
        const std::string_view projectId,
        std::string document)
    {
        requireNonBlank(document, "Indexed document");

        ProjectRecord& project =
            requireProject(projectId);

        const auto existing =
            std::find(
                project.indexedDocuments.begin(),
                project.indexedDocuments.end(),
                document);

        if (existing == project.indexedDocuments.end())
        {
            project.indexedDocuments.push_back(std::move(document));
            project.updatedUnixMilliseconds = currentUnixMilliseconds();
            persist();
        }
    }


    bool WorkspaceRepository::removeProjectIndexedDocument(
        const std::string_view projectId,
        const std::string_view document)
    {
        requireNonBlank(document, "Indexed document");

        ProjectRecord& project =
            requireProject(projectId);

        const auto oldSize = project.indexedDocuments.size();
        std::erase(
            project.indexedDocuments,
            std::string{ document });

        if (project.indexedDocuments.size() == oldSize)
        {
            return false;
        }

        project.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
        return true;
    }


    void WorkspaceRepository::addProjectMemoryRecord(
        const std::string_view projectId,
        const std::uint64_t memoryRecordId)
    {
        if (memoryRecordId == 0)
        {
            throw std::invalid_argument{
                "Project memory record id cannot be zero."
            };
        }

        ProjectRecord& project =
            requireProject(projectId);

        if (
            std::find(
                project.memoryRecordIds.begin(),
                project.memoryRecordIds.end(),
                memoryRecordId)
            == project.memoryRecordIds.end())
        {
            project.memoryRecordIds.push_back(memoryRecordId);
            project.updatedUnixMilliseconds = currentUnixMilliseconds();
            persist();
        }
    }


    void WorkspaceRepository::removeProjectMemoryRecord(
        const std::string_view projectId,
        const std::uint64_t memoryRecordId)
    {
        ProjectRecord& project =
            requireProject(projectId);

        const std::size_t removed =
            std::erase(
                project.memoryRecordIds,
                memoryRecordId);

        if (removed > 0)
        {
            project.updatedUnixMilliseconds = currentUnixMilliseconds();
            persist();
        }
    }


    void WorkspaceRepository::setProjectEnabledTools(
        const std::string_view projectId,
        std::vector<std::string> toolIds)
    {
        deduplicateNonBlank(toolIds, "Tool id");

        ProjectRecord& project =
            requireProject(projectId);

        project.enabledTools = std::move(toolIds);
        project.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
    }


    void WorkspaceRepository::setProjectSetting(
        const std::string_view projectId,
        std::string key,
        std::string value)
    {
        requireNonBlank(key, "Project setting key");

        ProjectRecord& project =
            requireProject(projectId);

        const auto existing =
            std::find_if(
                project.settings.begin(),
                project.settings.end(),
                [&key](const ProjectSetting& setting)
                {
                    return setting.key == key;
                });

        if (existing == project.settings.end())
        {
            project.settings.push_back(
                ProjectSetting{
                    .key = std::move(key),
                    .value = std::move(value)
                });
        }
        else
        {
            existing->value = std::move(value);
        }

        project.updatedUnixMilliseconds = currentUnixMilliseconds();
        persist();
    }


    void WorkspaceRepository::openDiscussion(
        const std::string_view discussionId)
    {
        (void) requireDiscussion(discussionId);
        if (!discussionVisible(discussionId))
        {
            throw std::logic_error{
                "Cannot open a removed Rose discussion or a discussion inside a removed project."
            };
        }

        const auto existing =
            std::find(
                snapshot_.openDiscussionIds.begin(),
                snapshot_.openDiscussionIds.end(),
                discussionId);

        if (existing == snapshot_.openDiscussionIds.end())
        {
            snapshot_.openDiscussionIds.emplace_back(discussionId);
            persist();
        }
    }


    void WorkspaceRepository::closeDiscussion(
        const std::string_view discussionId)
    {
        (void) requireDiscussion(discussionId);

        const auto originalSize =
            snapshot_.openDiscussionIds.size();

        std::erase(
            snapshot_.openDiscussionIds,
            discussionId);

        bool changed =
            snapshot_.openDiscussionIds.size() != originalSize;

        if (
            snapshot_.activeDiscussionId.has_value()
            && *snapshot_.activeDiscussionId == discussionId)
        {
            if (snapshot_.openDiscussionIds.empty())
            {
                snapshot_.activeDiscussionId.reset();
            }
            else
            {
                // Deterministic fallback: activate the most recently retained
                // open discussion. UI policy can become richer later.
                snapshot_.activeDiscussionId =
                    snapshot_.openDiscussionIds.back();
            }

            changed = true;
        }

        if (changed)
        {
            persist();
        }
    }


    void WorkspaceRepository::activateDiscussion(
        const std::string_view discussionId)
    {
        (void) requireDiscussion(discussionId);
        if (!discussionVisible(discussionId))
        {
            throw std::logic_error{
                "Cannot activate a removed Rose discussion or a discussion inside a removed project."
            };
        }

        const auto existing =
            std::find(
                snapshot_.openDiscussionIds.begin(),
                snapshot_.openDiscussionIds.end(),
                discussionId);

        if (existing == snapshot_.openDiscussionIds.end())
        {
            snapshot_.openDiscussionIds.emplace_back(discussionId);
        }

        snapshot_.activeDiscussionId =
            DiscussionId{ discussionId };

        persist();
    }


    std::vector<const DiscussionRecord*>
        WorkspaceRepository::discussionsForProject(
            const std::string_view projectId) const
    {
        if (findProject(projectId) == nullptr)
        {
            throw std::out_of_range{
                "Unknown Rose project id."
            };
        }

        std::vector<const DiscussionRecord*> matches;

        for (const DiscussionRecord& discussion : snapshot_.discussions)
        {
            if (
                !discussion.removed
                && discussion.projectId.has_value()
                && *discussion.projectId == projectId)
            {
                matches.push_back(&discussion);
            }
        }

        return matches;
    }


    bool WorkspaceRepository::discussionVisible(
        const std::string_view discussionId) const noexcept
    {
        const DiscussionRecord* discussion = findDiscussion(discussionId);
        if (discussion == nullptr || discussion->removed)
        {
            return false;
        }

        if (!discussion->projectId.has_value())
        {
            return true;
        }

        const ProjectRecord* project = findProject(*discussion->projectId);
        return project != nullptr && !project->removed;
    }


    ProjectRecord& WorkspaceRepository::requireProject(
        const std::string_view projectId)
    {
        const auto iterator =
            std::find_if(
                snapshot_.projects.begin(),
                snapshot_.projects.end(),
                [projectId](const ProjectRecord& project)
                {
                    return project.id == projectId;
                });

        if (iterator == snapshot_.projects.end())
        {
            throw std::out_of_range{
                "Unknown Rose project id."
            };
        }

        return *iterator;
    }


    DiscussionRecord& WorkspaceRepository::requireDiscussion(
        const std::string_view discussionId)
    {
        const auto iterator =
            std::find_if(
                snapshot_.discussions.begin(),
                snapshot_.discussions.end(),
                [discussionId](const DiscussionRecord& discussion)
                {
                    return discussion.id == discussionId;
                });

        if (iterator == snapshot_.discussions.end())
        {
            throw std::out_of_range{
                "Unknown Rose discussion id."
            };
        }

        return *iterator;
    }


    ProjectId WorkspaceRepository::generateProjectId()
    {
        while (true)
        {
            ProjectId candidate =
                makeId('p', nextIdSequence_++);

            if (findProject(candidate) == nullptr)
            {
                return candidate;
            }
        }
    }


    DiscussionId WorkspaceRepository::generateDiscussionId()
    {
        while (true)
        {
            DiscussionId candidate =
                makeId('d', nextIdSequence_++);

            if (findDiscussion(candidate) == nullptr)
            {
                return candidate;
            }
        }
    }


    void WorkspaceRepository::validateLoadedSnapshot() const
    {
        std::unordered_set<std::string> projectIds;

        for (const ProjectRecord& project : snapshot_.projects)
        {
            requireNonBlank(project.id, "Project id");
            requireNonBlank(project.title, "Project title");

            if (!projectIds.insert(project.id).second)
            {
                throw std::runtime_error{
                    "Rose workspace contains a duplicate project id."
                };
            }

            std::unordered_set<std::uint64_t> memoryIds;
            for (const std::uint64_t memoryId : project.memoryRecordIds)
            {
                if (memoryId == 0 || !memoryIds.insert(memoryId).second)
                {
                    throw std::runtime_error{
                        "Rose workspace project contains an invalid memory record id."
                    };
                }
            }

            std::unordered_set<std::string> settingKeys;
            for (const ProjectSetting& setting : project.settings)
            {
                requireNonBlank(setting.key, "Project setting key");

                if (!settingKeys.insert(setting.key).second)
                {
                    throw std::runtime_error{
                        "Rose workspace project contains a duplicate setting key."
                    };
                }
            }
        }

        std::unordered_set<std::string> discussionIds;

        for (const DiscussionRecord& discussion : snapshot_.discussions)
        {
            requireNonBlank(discussion.id, "Discussion id");
            requireNonBlank(discussion.title, "Discussion title");

            if (!discussionIds.insert(discussion.id).second)
            {
                throw std::runtime_error{
                    "Rose workspace contains a duplicate discussion id."
                };
            }

            if (
                discussion.projectId.has_value()
                && !projectIds.contains(*discussion.projectId))
            {
                throw std::runtime_error{
                    "Rose workspace discussion references an unknown project."
                };
            }
        }

        std::unordered_set<std::string> openIds;

        for (const DiscussionId& id : snapshot_.openDiscussionIds)
        {
            if (!discussionIds.contains(id))
            {
                throw std::runtime_error{
                    "Rose workspace open-discussion list references an unknown discussion."
                };
            }

            if (!discussionVisible(id))
            {
                throw std::runtime_error{
                    "Rose workspace open-discussion list references a removed discussion or project."
                };
            }

            if (!openIds.insert(id).second)
            {
                throw std::runtime_error{
                    "Rose workspace open-discussion list contains a duplicate id."
                };
            }
        }

        if (snapshot_.activeDiscussionId.has_value())
        {
            if (!discussionIds.contains(*snapshot_.activeDiscussionId))
            {
                throw std::runtime_error{
                    "Rose workspace active discussion does not exist."
                };
            }

            if (!discussionVisible(*snapshot_.activeDiscussionId))
            {
                throw std::runtime_error{
                    "Rose workspace active discussion is removed or belongs to a removed project."
                };
            }

            if (!openIds.contains(*snapshot_.activeDiscussionId))
            {
                throw std::runtime_error{
                    "Rose workspace active discussion must also be open."
                };
            }
        }
    }


    void WorkspaceRepository::persist()
    {
        store_.save(snapshot_);
    }

} // namespace rose::workspace
