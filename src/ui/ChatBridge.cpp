#include "ui/ChatBridge.h"

#include <utility>

namespace rose::ui
{

    void ChatBridge::submitUserSubmission(
        input::UserSubmission submission)
    {
        if (
            submission.text.empty()
            && submission.attachments.empty())
        {
            return;
        }

        {
            std::lock_guard lock{
                mutex_
            };

            if (shutdownRequested_)
            {
                return;
            }

            workerRequests_.emplace_back(
                std::move(submission));
        }

        requestAvailable_.notify_one();
    }


    void ChatBridge::submitUserMessage(
        std::string message)
    {
        submitUserSubmission(
            input::UserSubmission{
                .text = std::move(message),
                .attachments = {}
            });
    }


    void ChatBridge::submitUiCommand(
        UiWorkerCommand command)
    {
        {
            std::lock_guard lock{
                mutex_
            };

            if (shutdownRequested_)
            {
                return;
            }

            workerRequests_.emplace_back(
                std::move(command));
        }

        requestAvailable_.notify_one();
    }


    std::optional<WorkerRequest>
        ChatBridge::waitForWorkerRequest()
    {
        std::unique_lock lock{
            mutex_
        };

        requestAvailable_.wait(
            lock,
            [this]()
            {
                return
                    shutdownRequested_
                    || !workerRequests_.empty();
            });

        if (
            shutdownRequested_
            && workerRequests_.empty())
        {
            return std::nullopt;
        }

        WorkerRequest request =
            std::move(
                workerRequests_.front());

        workerRequests_.pop_front();

        return request;
    }


    void ChatBridge::postEvent(
        ChatEvent event)
    {
        std::lock_guard lock{
            mutex_
        };

        if (shutdownRequested_)
        {
            return;
        }

        // Token callbacks can outpace the SDL frame loop. Merge adjacent text
        // fragments while preserving start/finish and artifact ordering.
        constexpr std::size_t maximumMergedTextBytes{ 16u * 1024u };
        if (event.type == ChatEventType::AssistantText
            && !events_.empty()
            && events_.back().type == ChatEventType::AssistantText
            && events_.back().text.size() + event.text.size()
                <= maximumMergedTextBytes)
        {
            events_.back().text += event.text;
            return;
        }

        events_.push_back(
            std::move(event));
    }


    std::optional<ChatEvent>
        ChatBridge::tryPopEvent()
    {
        std::lock_guard lock{
            mutex_
        };

        if (events_.empty())
        {
            return std::nullopt;
        }

        ChatEvent event =
            std::move(
                events_.front());

        events_.pop_front();

        return event;
    }


    void ChatBridge::publishWorkspaceSnapshot(
        workspace::WorkspaceSnapshot snapshot)
    {
        std::lock_guard lock{
            mutex_
        };

        if (shutdownRequested_)
        {
            return;
        }

        workspaceSnapshot_ = std::move(snapshot);
        ++workspaceSnapshotVersion_;

        // Keep zero reserved for "no snapshot observed yet" even after an
        // astronomically unlikely unsigned wraparound.
        if (workspaceSnapshotVersion_ == 0)
        {
            ++workspaceSnapshotVersion_;
        }
    }


    std::optional<WorkspaceSnapshotUpdate>
        ChatBridge::workspaceSnapshotSince(
            const std::uint64_t knownVersion) const
    {
        std::lock_guard lock{
            mutex_
        };

        if (
            workspaceSnapshotVersion_ == 0
            || workspaceSnapshotVersion_ == knownVersion)
        {
            return std::nullopt;
        }

        return WorkspaceSnapshotUpdate{
            .version = workspaceSnapshotVersion_,
            .snapshot = workspaceSnapshot_
        };
    }


    void ChatBridge::requestShutdown()
    {
        {
            std::lock_guard lock{
                mutex_
            };

            shutdownRequested_ = true;
        }

        requestAvailable_.notify_all();
    }


    bool ChatBridge::shutdownRequested() const
    {
        std::lock_guard lock{
            mutex_
        };

        return shutdownRequested_;
    }

} // namespace rose::ui
