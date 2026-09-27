#pragma once

#include "knowledge/IProjectKnowledgeStore.h"

#include <cstddef>
#include <shared_mutex>
#include <string_view>
#include <vector>

namespace rose::knowledge
{
    // Thread-safe in-memory project knowledge catalog.
    //
    // Search is read-only and may run on Rose's conversation worker while the
    // dedicated indexing worker replaces another project's index. Persistence is
    // performed while holding the write lock so the on-disk snapshot always
    // corresponds to a complete in-memory state.
    class ProjectKnowledgeRepository final
    {
    public:
        explicit ProjectKnowledgeRepository(
            IProjectKnowledgeStore& store);

        ProjectKnowledgeRepository(const ProjectKnowledgeRepository&) = delete;
        ProjectKnowledgeRepository& operator=(const ProjectKnowledgeRepository&) = delete;

        void replaceProject(
            ProjectKnowledgeRecord project);

        void clearProject(
            std::string_view projectId);

        [[nodiscard]]
        ProjectKnowledgeStats stats(
            std::string_view projectId) const;

        [[nodiscard]]
        std::vector<KnowledgeSearchHit> search(
            std::string_view projectId,
            std::string_view query,
            std::size_t maximumResults = 5) const;

    private:
        void validateLoadedSnapshot() const;
        void persistLocked();

        IProjectKnowledgeStore& store_;
        ProjectKnowledgeSnapshot snapshot_;
        mutable std::shared_mutex mutex_;
    };
}
