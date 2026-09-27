#include "jobs/ErrandCommand.h"
#include "jobs/FileJobStore.h"
#include "jobs/JobRepository.h"
#include "jobs/PersistentJobScheduler.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
    void require(const bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    std::filesystem::path testPath()
    {
        const auto root = std::filesystem::temp_directory_path() / "rose_background_jobs_test";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        error.clear();
        std::filesystem::create_directories(root, error);
        if (error)
        {
            throw std::runtime_error{ "Could not create Rose background-job test directory." };
        }
        return root / "jobs.rosejobs";
    }
}

int main()
{
    try
    {
        const auto parsed = rose::jobs::parseErrandCommand("/errand in 10m Stretch");
        require(parsed.has_value(), "Reminder parser did not recognize /errand.");
        require(parsed->kind == rose::jobs::ErrandCommandKind::ScheduleReminder, "Wrong errand kind.");
        require(parsed->delayMilliseconds == 600'000, "Reminder parser converted minutes incorrectly.");
        require(parsed->text == "Stretch", "Reminder parser lost reminder text.");

        const auto path = testPath();
        rose::jobs::JobId persistedId;

        {
            rose::jobs::FileJobStore store{ path };
            rose::jobs::JobRepository repository{ store };
            const auto job = repository.createReminder(
                rose::jobs::ReminderRequest{
                    .text = "Long-lived reminder",
                    .delayMilliseconds = 60'000,
                    .projectId = "project-test",
                    .discussionId = "discussion-test"
                });
            persistedId = job.id;
            require(repository.snapshot().jobs.size() == 1, "Reminder was not added to repository.");
        }

        {
            rose::jobs::FileJobStore store{ path };
            rose::jobs::JobRepository repository{ store };
            const auto* job = repository.find(persistedId);
            require(job != nullptr, "Persisted reminder did not reload.");
            require(job->projectId == "project-test", "Project provenance was not persisted.");
            require(job->discussionId == "discussion-test", "Discussion provenance was not persisted.");
            require(repository.cancel(persistedId), "Pending reminder could not be cancelled.");
        }

        // Simulate a process crash after a reminder was marked Running.
        // On restart, Reminder is intentionally retryable and must become Pending.
        {
            rose::jobs::FileJobStore store{ path };
            rose::jobs::JobSnapshot interrupted;
            interrupted.jobs.push_back(
                rose::jobs::JobRecord{
                    .id = "rem-interrupted",
                    .kind = rose::jobs::JobKind::Reminder,
                    .status = rose::jobs::JobStatus::Running,
                    .text = "Recover me",
                    .lastError = {},
                    .projectId = std::nullopt,
                    .discussionId = std::nullopt,
                    .dueUnixMilliseconds = 1,
                    .createdUnixMilliseconds = 1,
                    .updatedUnixMilliseconds = 1
                });
            store.save(interrupted);
        }

        {
            rose::jobs::FileJobStore store{ path };
            rose::jobs::JobRepository repository{ store };
            const auto* recovered = repository.find("rem-interrupted");
            require(
                recovered != nullptr && recovered->status == rose::jobs::JobStatus::Pending,
                "Interrupted Running reminder was not recovered to Pending.");
        }

        std::error_code cleanupError;
        std::filesystem::remove(path, cleanupError);
        std::filesystem::remove(path.string() + ".bak", cleanupError);
        std::filesystem::remove(path.string() + ".tmp", cleanupError);

        std::atomic<int> fired{ 0 };
        {
            rose::jobs::PersistentJobScheduler scheduler{
                path,
                [&fired](const rose::jobs::JobRecord& job)
                {
                    if (job.text == "Fire quickly")
                    {
                        fired.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            };

            const auto quick = scheduler.scheduleReminder(
                rose::jobs::ReminderRequest{
                    .text = "Fire quickly",
                    .delayMilliseconds = 60,
                    .projectId = std::nullopt,
                    .discussionId = std::nullopt
                });

            const auto cancelMe = scheduler.scheduleReminder(
                rose::jobs::ReminderRequest{
                    .text = "Do not fire",
                    .delayMilliseconds = 5'000,
                    .projectId = std::nullopt,
                    .discussionId = std::nullopt
                });
            require(scheduler.cancel(cancelMe.id), "Scheduler could not cancel a pending reminder.");

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
            while (fired.load(std::memory_order_relaxed) == 0 && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
            }

            require(fired.load(std::memory_order_relaxed) == 1, "Due reminder did not fire once.");

            const auto snapshot = scheduler.snapshot();
            const auto findById = [&](const rose::jobs::JobId& id) -> const rose::jobs::JobRecord*
            {
                for (const auto& job : snapshot.jobs)
                {
                    if (job.id == id) return &job;
                }
                return nullptr;
            };

            const auto* firedJob = findById(quick.id);
            const auto* cancelledJob = findById(cancelMe.id);
            require(firedJob != nullptr && firedJob->status == rose::jobs::JobStatus::Completed,
                "Fired reminder did not persist Completed state.");
            require(cancelledJob != nullptr && cancelledJob->status == rose::jobs::JobStatus::Cancelled,
                "Cancelled reminder did not persist Cancelled state.");
        }

        std::cout << "Rose BackgroundJobs tests: PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rose BackgroundJobs tests: FAIL: " << exception.what() << '\n';
        return 1;
    }
}
