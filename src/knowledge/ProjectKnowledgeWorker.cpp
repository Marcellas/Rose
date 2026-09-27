#include "knowledge/ProjectKnowledgeWorker.h"

#include "knowledge/ProjectKnowledgeRepository.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace rose::knowledge
{
    ProjectKnowledgeWorker::ProjectKnowledgeWorker(
        ProjectKnowledgeRepository& repository,
        ProjectKnowledgeWorkerCallback callback,
        const ProjectKnowledgeIndexerConfig config)
        : repository_{ repository }
        , callback_{ std::move(callback) }
        , indexer_{ config }
        , thread_{ [this](const std::stop_token stopToken)
            {
                run(stopToken);
            } }
    {
        if (!callback_)
        {
            throw std::invalid_argument{
                "ProjectKnowledgeWorker requires an event callback."
            };
        }
    }

    ProjectKnowledgeWorker::~ProjectKnowledgeWorker()
    {
        thread_.request_stop();
        requestAvailable_.notify_all();
    }

    void ProjectKnowledgeWorker::requestIndex(
        std::string projectId,
        std::vector<std::string> approvedRoots)
    {
        if (projectId.empty())
        {
            throw std::invalid_argument{
                "Project knowledge worker requires a project id."
            };
        }
        if (approvedRoots.empty())
        {
            throw std::invalid_argument{
                "The active project has no approved knowledge roots."
            };
        }

        {
            std::lock_guard lock{ mutex_ };

            // A newer refresh supersedes an older queued refresh for the same
            // project. An already-running request is allowed to finish atomically.
            std::erase_if(
                requests_,
                [&](const Request& request)
                {
                    return request.projectId == projectId;
                });

            requests_.push_back(
                Request{
                    .projectId = std::move(projectId),
                    .approvedRoots = std::move(approvedRoots)
                });
        }
        requestAvailable_.notify_one();
    }

    void ProjectKnowledgeWorker::run(
        const std::stop_token stopToken)
    {
        while (!stopToken.stop_requested())
        {
            Request request;
            {
                std::unique_lock lock{ mutex_ };
                requestAvailable_.wait(
                    lock,
                    stopToken,
                    [&]
                    {
                        return !requests_.empty();
                    });

                if (stopToken.stop_requested())
                {
                    return;
                }
                if (requests_.empty())
                {
                    continue;
                }

                request = std::move(requests_.front());
                requests_.pop_front();
            }

            try
            {
                ProjectKnowledgeIndexResult result = indexer_.index(
                    request.projectId,
                    request.approvedRoots,
                    stopToken);

                if (stopToken.stop_requested() || result.report.cancelled)
                {
                    continue;
                }

                repository_.replaceProject(
                    std::move(result.project));

                callback_(
                    ProjectKnowledgeWorkerEvent{
                        .type = ProjectKnowledgeWorkerEventType::IndexCompleted,
                        .projectId = request.projectId,
                        .report = std::move(result.report),
                        .message = "Project knowledge indexing completed."
                    });
            }
            catch (const std::exception& exception)
            {
                callback_(
                    ProjectKnowledgeWorkerEvent{
                        .type = ProjectKnowledgeWorkerEventType::IndexFailed,
                        .projectId = request.projectId,
                        .report = {},
                        .message = exception.what()
                    });
            }
            catch (...)
            {
                callback_(
                    ProjectKnowledgeWorkerEvent{
                        .type = ProjectKnowledgeWorkerEventType::IndexFailed,
                        .projectId = request.projectId,
                        .report = {},
                        .message = "Project knowledge indexing failed with an unknown error."
                    });
            }
        }
    }
}
