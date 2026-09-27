#pragma once

#include "knowledge/ProjectKnowledgeIndexer.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

namespace rose::knowledge
{
    class ProjectKnowledgeRepository;

    enum class ProjectKnowledgeWorkerEventType
    {
        IndexCompleted,
        IndexFailed
    };

    struct ProjectKnowledgeWorkerEvent
    {
        ProjectKnowledgeWorkerEventType type{
            ProjectKnowledgeWorkerEventType::IndexCompleted
        };
        std::string projectId;
        ProjectKnowledgeIndexReport report;
        std::string message;
    };

    using ProjectKnowledgeWorkerCallback =
        std::function<void(ProjectKnowledgeWorkerEvent)>;

    // Dedicated filesystem indexing worker. The main Rose worker only enqueues an
    // immutable project id + approved-root snapshot; it never performs the disk
    // crawl itself. ProjectKnowledgeRepository provides the synchronized handoff
    // between background writes and foreground retrieval.
    class ProjectKnowledgeWorker final
    {
    public:
        ProjectKnowledgeWorker(
            ProjectKnowledgeRepository& repository,
            ProjectKnowledgeWorkerCallback callback,
            ProjectKnowledgeIndexerConfig config = {});

        ~ProjectKnowledgeWorker();

        ProjectKnowledgeWorker(const ProjectKnowledgeWorker&) = delete;
        ProjectKnowledgeWorker& operator=(const ProjectKnowledgeWorker&) = delete;

        void requestIndex(
            std::string projectId,
            std::vector<std::string> approvedRoots);

    private:
        struct Request
        {
            std::string projectId;
            std::vector<std::string> approvedRoots;
        };

        void run(std::stop_token stopToken);

        ProjectKnowledgeRepository& repository_;
        ProjectKnowledgeWorkerCallback callback_;
        ProjectKnowledgeIndexer indexer_;
        std::mutex mutex_;
        std::condition_variable_any requestAvailable_;
        std::deque<Request> requests_;
        std::jthread thread_;
    };
}
