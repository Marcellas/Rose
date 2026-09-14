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

            userSubmissions_.push_back(
                std::move(submission));
        }

        requestAvailable_.notify_one();
    }


    std::optional<input::UserSubmission>
        ChatBridge::waitForUserSubmission()
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
                    || !userSubmissions_.empty();
            });

        if (
            shutdownRequested_
            && userSubmissions_.empty())
        {
            return std::nullopt;
        }

        input::UserSubmission submission =
            std::move(
                userSubmissions_.front());

        userSubmissions_.pop_front();

        return submission;
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
