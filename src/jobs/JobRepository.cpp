#include "jobs/JobRepository.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>

namespace rose::jobs
{
    namespace
    {
        std::int64_t unixMillisecondsNow()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }

    JobRepository::JobRepository(IJobStore& store)
        : store_{ store }
        , snapshot_{ store_.load() }
    {
        recoverInterruptedJobs();
    }

    const JobSnapshot& JobRepository::snapshot() const noexcept
    {
        return snapshot_;
    }

    const JobRecord* JobRepository::find(const std::string_view id) const noexcept
    {
        const auto iterator = std::find_if(
            snapshot_.jobs.begin(),
            snapshot_.jobs.end(),
            [id](const JobRecord& job) { return job.id == id; });

        return iterator == snapshot_.jobs.end() ? nullptr : &*iterator;
    }

    JobRecord* JobRepository::findMutable(const std::string_view id) noexcept
    {
        const auto iterator = std::find_if(
            snapshot_.jobs.begin(),
            snapshot_.jobs.end(),
            [id](const JobRecord& job) { return job.id == id; });

        return iterator == snapshot_.jobs.end() ? nullptr : &*iterator;
    }

    JobRecord JobRepository::createReminder(const ReminderRequest& request)
    {
        if (request.text.empty())
        {
            throw std::invalid_argument{ "A Rose reminder needs some reminder text." };
        }
        if (request.delayMilliseconds <= 0)
        {
            throw std::invalid_argument{ "A Rose reminder delay must be greater than zero." };
        }

        const std::int64_t now = unixMillisecondsNow();
        if (request.delayMilliseconds > std::numeric_limits<std::int64_t>::max() - now)
        {
            throw std::overflow_error{ "Rose reminder delay is too large." };
        }

        JobRecord job;
        job.id = makeId(now);
        job.kind = JobKind::Reminder;
        job.status = JobStatus::Pending;
        job.text = request.text;
        job.projectId = request.projectId;
        job.discussionId = request.discussionId;
        job.dueUnixMilliseconds = now + request.delayMilliseconds;
        job.createdUnixMilliseconds = now;
        job.updatedUnixMilliseconds = now;

        snapshot_.jobs.push_back(job);
        persist();
        return job;
    }

    bool JobRepository::cancel(const std::string_view id)
    {
        JobRecord* job = findMutable(id);
        if (job == nullptr)
        {
            return false;
        }
        if (job->status != JobStatus::Pending)
        {
            return false;
        }

        job->status = JobStatus::Cancelled;
        job->updatedUnixMilliseconds = unixMillisecondsNow();
        persist();
        return true;
    }

    std::vector<JobId> JobRepository::dueJobIds(const std::int64_t nowUnixMilliseconds) const
    {
        std::vector<JobId> result;
        for (const JobRecord& job : snapshot_.jobs)
        {
            if (
                job.status == JobStatus::Pending
                && job.dueUnixMilliseconds <= nowUnixMilliseconds)
            {
                result.push_back(job.id);
            }
        }
        return result;
    }

    std::optional<std::int64_t> JobRepository::nextDueUnixMilliseconds() const
    {
        std::optional<std::int64_t> result;
        for (const JobRecord& job : snapshot_.jobs)
        {
            if (job.status != JobStatus::Pending)
            {
                continue;
            }
            if (!result || job.dueUnixMilliseconds < *result)
            {
                result = job.dueUnixMilliseconds;
            }
        }
        return result;
    }

    JobRecord JobRepository::markRunning(const std::string_view id)
    {
        JobRecord* job = findMutable(id);
        if (job == nullptr || job->status != JobStatus::Pending)
        {
            throw std::runtime_error{ "Rose attempted to run an unavailable background job." };
        }

        job->status = JobStatus::Running;
        job->updatedUnixMilliseconds = unixMillisecondsNow();
        persist();
        return *job;
    }

    JobRecord JobRepository::markCompleted(const std::string_view id)
    {
        JobRecord* job = findMutable(id);
        if (job == nullptr || job->status != JobStatus::Running)
        {
            throw std::runtime_error{ "Rose attempted to complete a background job that was not running." };
        }

        job->status = JobStatus::Completed;
        job->lastError.clear();
        job->updatedUnixMilliseconds = unixMillisecondsNow();
        persist();
        return *job;
    }

    JobRecord JobRepository::markFailed(
        const std::string_view id,
        const std::string_view error)
    {
        JobRecord* job = findMutable(id);
        if (job == nullptr || job->status != JobStatus::Running)
        {
            throw std::runtime_error{ "Rose attempted to fail a background job that was not running." };
        }

        job->status = JobStatus::Failed;
        job->lastError = std::string{ error };
        job->updatedUnixMilliseconds = unixMillisecondsNow();
        persist();
        return *job;
    }

    JobId JobRepository::makeId(const std::int64_t nowUnixMilliseconds)
    {
        while (true)
        {
            ++idSequence_;
            JobId candidate =
                "rem-" + std::to_string(nowUnixMilliseconds)
                + "-" + std::to_string(idSequence_);

            if (find(candidate) == nullptr)
            {
                return candidate;
            }
        }
    }

    void JobRepository::persist()
    {
        store_.save(snapshot_);
    }

    void JobRepository::recoverInterruptedJobs()
    {
        bool changed = false;
        const std::int64_t now = unixMillisecondsNow();

        for (JobRecord& job : snapshot_.jobs)
        {
            if (job.status == JobStatus::Running)
            {
                // The process cannot know whether a previous callback completed
                // after a crash. Reminders are idempotent enough to retry and this
                // is safer than silently losing them. Consequential future job
                // kinds will need explicit recovery semantics before being added.
                job.status = JobStatus::Pending;
                job.updatedUnixMilliseconds = now;
                changed = true;
            }
        }

        if (changed)
        {
            persist();
        }
    }
}
