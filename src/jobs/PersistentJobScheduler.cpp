#include "jobs/PersistentJobScheduler.h"

#include "jobs/FileJobStore.h"
#include "jobs/JobRepository.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

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

    PersistentJobScheduler::PersistentJobScheduler(
        std::filesystem::path persistencePath,
        ReminderFiredCallback reminderFired)
        : persistencePath_{ std::move(persistencePath) }
        , reminderFired_{ std::move(reminderFired) }
        , worker_{ [this]() { workerMain(); } }
    {
        std::unique_lock lock{ initializationMutex_ };
        initializationCv_.wait(
            lock,
            [this]() { return initializationFinished_; });

        if (initializationError_)
        {
            {
                std::lock_guard queueLock{ mutex_ };
                stopping_ = true;
            }
            wakeup_.notify_all();
            lock.unlock();

            if (worker_.joinable())
            {
                worker_.join();
            }
            std::rethrow_exception(initializationError_);
        }
    }

    PersistentJobScheduler::~PersistentJobScheduler()
    {
        {
            std::lock_guard lock{ mutex_ };
            stopping_ = true;
        }
        wakeup_.notify_all();

        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    JobRecord PersistentJobScheduler::scheduleReminder(ReminderRequest request)
    {
        ScheduleCommand command;
        command.request = std::move(request);
        std::future<JobRecord> result = command.completion.get_future();

        {
            std::lock_guard lock{ mutex_ };
            if (stopping_)
            {
                throw std::runtime_error{ "Rose background scheduler is stopping." };
            }
            commands_.emplace_back(std::move(command));
        }

        wakeup_.notify_one();
        return result.get();
    }

    bool PersistentJobScheduler::cancel(std::string jobId)
    {
        CancelCommand command;
        command.jobId = std::move(jobId);
        std::future<bool> result = command.completion.get_future();

        {
            std::lock_guard lock{ mutex_ };
            if (stopping_)
            {
                return false;
            }
            commands_.emplace_back(std::move(command));
        }

        wakeup_.notify_one();
        return result.get();
    }

    JobSnapshot PersistentJobScheduler::snapshot() const
    {
        std::lock_guard lock{ snapshotMutex_ };
        return snapshot_;
    }

    void PersistentJobScheduler::workerMain()
    {
        try
        {
            FileJobStore store{ persistencePath_ };
            JobRepository repository{ store };
            publishSnapshot(repository.snapshot());
            finishInitialization();

            while (true)
            {
                std::deque<Command> commands;

                {
                    std::unique_lock lock{ mutex_ };

                    const auto nextDue = repository.nextDueUnixMilliseconds();
                    if (commands_.empty() && !stopping_)
                    {
                        if (nextDue)
                        {
                            const auto deadline =
                                std::chrono::system_clock::time_point{
                                    std::chrono::milliseconds{ *nextDue }
                                };

                            wakeup_.wait_until(
                                lock,
                                deadline,
                                [this]() { return stopping_ || !commands_.empty(); });
                        }
                        else
                        {
                            wakeup_.wait(
                                lock,
                                [this]() { return stopping_ || !commands_.empty(); });
                        }
                    }

                    commands.swap(commands_);

                    if (stopping_ && commands.empty())
                    {
                        break;
                    }
                }

                for (Command& genericCommand : commands)
                {
                    std::visit(
                        [&](auto& command)
                        {
                            try
                            {
                                using CommandType = std::decay_t<decltype(command)>;

                                if constexpr (std::is_same_v<CommandType, ScheduleCommand>)
                                {
                                    JobRecord job = repository.createReminder(command.request);
                                    publishSnapshot(repository.snapshot());
                                    command.completion.set_value(std::move(job));
                                }
                                else
                                {
                                    const bool cancelled = repository.cancel(command.jobId);
                                    publishSnapshot(repository.snapshot());
                                    command.completion.set_value(cancelled);
                                }
                            }
                            catch (...)
                            {
                                command.completion.set_exception(std::current_exception());
                            }
                        },
                        genericCommand);
                }

                const std::vector<JobId> dueIds =
                    repository.dueJobIds(unixMillisecondsNow());

                for (const JobId& id : dueIds)
                {
                    JobRecord running = repository.markRunning(id);
                    publishSnapshot(repository.snapshot());

                    try
                    {
                        if (reminderFired_)
                        {
                            reminderFired_(running);
                        }

                        (void)repository.markCompleted(id);
                    }
                    catch (const std::exception& exception)
                    {
                        (void)repository.markFailed(id, exception.what());
                    }
                    catch (...)
                    {
                        (void)repository.markFailed(id, "Unknown reminder callback failure.");
                    }

                    publishSnapshot(repository.snapshot());
                }
            }
        }
        catch (...)
        {
            finishInitialization(std::current_exception());

            // Unblock any callers whose commands were queued before the fatal
            // scheduler failure became visible.
            std::deque<Command> abandoned;
            {
                std::lock_guard lock{ mutex_ };
                stopping_ = true;
                abandoned.swap(commands_);
            }

            const std::exception_ptr error = std::current_exception();
            for (Command& genericCommand : abandoned)
            {
                std::visit(
                    [&](auto& command)
                    {
                        try
                        {
                            command.completion.set_exception(error);
                        }
                        catch (...)
                        {
                        }
                    },
                    genericCommand);
            }
        }
    }

    void PersistentJobScheduler::publishSnapshot(const JobSnapshot& snapshot)
    {
        std::lock_guard lock{ snapshotMutex_ };
        snapshot_ = snapshot;
    }

    void PersistentJobScheduler::finishInitialization(std::exception_ptr error)
    {
        std::lock_guard lock{ initializationMutex_ };
        if (initializationFinished_)
        {
            return;
        }

        initializationError_ = error;
        initializationFinished_ = true;
        initializationCv_.notify_all();
    }
}
