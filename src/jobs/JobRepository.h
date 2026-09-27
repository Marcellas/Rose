#pragma once

#include "jobs/IJobStore.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rose::jobs
{
    // Mutable job state with one persistence boundary. The scheduler thread is the
    // sole owner in production; no internal locking is required here.
    class JobRepository final
    {
    public:
        explicit JobRepository(IJobStore& store);

        [[nodiscard]] const JobSnapshot& snapshot() const noexcept;
        [[nodiscard]] const JobRecord* find(std::string_view id) const noexcept;

        [[nodiscard]] JobRecord createReminder(const ReminderRequest& request);
        [[nodiscard]] bool cancel(std::string_view id);

        [[nodiscard]] std::vector<JobId> dueJobIds(std::int64_t nowUnixMilliseconds) const;
        [[nodiscard]] std::optional<std::int64_t> nextDueUnixMilliseconds() const;

        [[nodiscard]] JobRecord markRunning(std::string_view id);
        [[nodiscard]] JobRecord markCompleted(std::string_view id);
        [[nodiscard]] JobRecord markFailed(std::string_view id, std::string_view error);

    private:
        [[nodiscard]] JobRecord* findMutable(std::string_view id) noexcept;
        [[nodiscard]] JobId makeId(std::int64_t nowUnixMilliseconds);
        void persist();
        void recoverInterruptedJobs();

        IJobStore& store_;
        JobSnapshot snapshot_;
        std::uint64_t idSequence_{ 0 };
    };
}
