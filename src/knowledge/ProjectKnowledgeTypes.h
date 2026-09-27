#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rose::knowledge
{
    struct KnowledgeChunkRecord
    {
        std::size_t ordinal{ 0 };
        std::string sourceLocator;
        std::string text;
    };

    struct KnowledgeDocumentRecord
    {
        std::string sourcePath;
        std::string contentKind;
        std::string readerId;
        std::uintmax_t sourceBytes{ 0 };
        std::int64_t modifiedUnixMilliseconds{ 0 };
        std::vector<KnowledgeChunkRecord> chunks;
    };

    struct ProjectKnowledgeRecord
    {
        std::string projectId;
        std::int64_t indexedUnixMilliseconds{ 0 };
        std::vector<KnowledgeDocumentRecord> documents;
    };

    struct ProjectKnowledgeSnapshot
    {
        std::vector<ProjectKnowledgeRecord> projects;
    };

    struct KnowledgeSearchHit
    {
        std::string sourcePath;
        std::string sourceLocator;
        std::string contentKind;
        std::string readerId;
        std::size_t chunkOrdinal{ 0 };
        double score{ 0.0 };
        std::string excerpt;
    };

    struct ProjectKnowledgeStats
    {
        std::size_t documentCount{ 0 };
        std::size_t chunkCount{ 0 };
        std::uintmax_t sourceBytes{ 0 };
        std::int64_t indexedUnixMilliseconds{ 0 };
    };

    struct ProjectKnowledgeIndexReport
    {
        std::string projectId;
        std::size_t rootsScanned{ 0 };
        std::size_t documentsIndexed{ 0 };
        std::size_t chunksCreated{ 0 };
        std::uintmax_t sourceBytes{ 0 };
        std::size_t filesSkipped{ 0 };
        bool cancelled{ false };
        std::vector<std::string> warnings;
    };
}
