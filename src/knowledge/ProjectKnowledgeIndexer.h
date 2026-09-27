#pragma once

#include "knowledge/ProjectContentReader.h"
#include "knowledge/ProjectKnowledgeTypes.h"

#include <cstddef>
#include <stop_token>
#include <string>
#include <vector>

namespace rose::knowledge
{
    struct ProjectKnowledgeIndexerConfig
    {
        std::size_t maximumDepth{ 8 };
        std::size_t maximumFiles{ 4000 };
        std::size_t maximumFileBytes{ 64u * 1024u * 1024u };
        std::size_t maximumChunkBytes{ 1400 };
        std::size_t chunkOverlapBytes{ 180 };
        std::size_t maximumWarnings{ 24 };
    };

    struct ProjectKnowledgeIndexResult
    {
        ProjectKnowledgeRecord project;
        ProjectKnowledgeIndexReport report;
    };

    // Reads explicitly approved project roots through format adapters and converts
    // extracted text-bearing segments into bounded overlapping chunks. It never
    // follows directory symlinks and ignores common generated/dependency directories.
    //
    // The indexer knows nothing about PDF, Office, images, archives, programming
    // languages, or other specific formats. Those belong to IProjectContentReader
    // implementations so new file understanding can be added without changing the
    // persistence/retrieval pipeline.
    class ProjectKnowledgeIndexer final
    {
    public:
        explicit ProjectKnowledgeIndexer(
            ProjectKnowledgeIndexerConfig config = {},
            ProjectContentReaderRegistry contentReaders =
                makeDefaultProjectContentReaderRegistry());

        [[nodiscard]]
        ProjectKnowledgeIndexResult index(
            std::string projectId,
            const std::vector<std::string>& approvedRoots,
            std::stop_token stopToken = {}) const;

    private:
        ProjectKnowledgeIndexerConfig config_;
        ProjectContentReaderRegistry contentReaders_;
    };
}
