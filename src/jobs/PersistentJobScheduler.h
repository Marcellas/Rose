#pragma once

#include "jobs/JobTypes.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>

namespace rose::jobs
{
    // Persistent one-shot background scheduler.
    //
    // Ownership/lifetime:
    //   * one dedicated thread owns FileJobStore + JobRepository;
    //   * callers communicate through a small command queue;
    //   * snapshot() returns a protected value copy;
    //   * the completion callback runs on the scheduler thread and therefore
    //     must not touch SDL resources directly.
    class PersistentJobScheduler final
    {
    public:
        using ReminderFiredCallback = std::function<void(const JobRecord&)>;

        PersistentJobScheduler(
            std::filesystem::path persistencePath,
            ReminderFiredCallback reminderFired);
        ~PersistentJobScheduler();

        PersistentJobScheduler(const PersistentJobScheduler&) = delete;
        PersistentJobScheduler& operator=(const PersistentJobScheduler&) = delete;

        [[nodiscard]] JobRecord scheduleReminder(ReminderRequest request);
        [[nodiscard]] bool cancel(std::string jobId);
        [[nodiscard]] JobSnapshot snapshot() const;

    private:
        struct ScheduleCommand
        {
            ReminderRequest request;
            std::promise<JobRecord> completion;
        };

        struct CancelCommand
        {
            std::string jobId;
            std::promise<bool> completion;
        };

        using Command = std::variant<ScheduleCommand, CancelCommand>;

        void workerMain();
        void publishSnapshot(const JobSnapshot& snapshot);
        void finishInitialization(std::exception_ptr error = nullptr);

        std::filesystem::path persistencePath_;
        ReminderFiredCallback reminderFired_;

        mutable std::mutex mutex_;
        std::condition_variable wakeup_;
        std::deque<Command> commands_;
        bool stopping_{ false };

        mutable std::mutex snapshotMutex_;
        JobSnapshot snapshot_;

        std::mutex initializationMutex_;
        std::condition_variable initializationCv_;
        bool initializationFinished_{ false };
        std::exception_ptr initializationError_;

        std::thread worker_;
    };
}
