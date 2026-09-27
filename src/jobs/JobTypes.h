#pragma once

#include "workspace/WorkspaceTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rose::jobs
{
    using JobId = std::string;

    enum class JobKind : std::uint8_t
    {
        Reminder = 1
    };

    enum class JobStatus : std::uint8_t
    {
        Pending = 1,
        Running = 2,
        Completed = 3,
        Cancelled = 4,
        Failed = 5
    };

    // Provider-neutral durable description of background work.
    //
    // Batch 22 intentionally supports Reminder only. The record is generic enough
    // for later local jobs, but adding a new JobKind does NOT automatically grant
    // that job tool/network permission. Execution policy stays elsewhere.
    struct JobRecord
    {
        JobId id;
        JobKind kind{ JobKind::Reminder };
        JobStatus status{ JobStatus::Pending };

        std::string text;
        std::string lastError;

        std::optional<workspace::ProjectId> projectId;
        std::optional<workspace::DiscussionId> discussionId;

        std::int64_t dueUnixMilliseconds{ 0 };
        std::int64_t createdUnixMilliseconds{ 0 };
        std::int64_t updatedUnixMilliseconds{ 0 };
    };

    struct JobSnapshot
    {
        std::vector<JobRecord> jobs;
    };

    struct ReminderRequest
    {
        std::string text;
        std::int64_t delayMilliseconds{ 0 };
        std::optional<workspace::ProjectId> projectId;
        std::optional<workspace::DiscussionId> discussionId;
    };
}
