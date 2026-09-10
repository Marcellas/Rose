#include "ui/ChatBridge.h"

#include <utility>


namespace rose::ui
{

    void ChatBridge::submitUserMessage(
        std::string message)
    {
        if (message.empty())
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


            userMessages_.push_back(
                std::move(message));
        }


        requestAvailable_.notify_one();
    }


    std::optional<std::string>
        ChatBridge::waitForUserMessage()
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
                    || !userMessages_.empty();
            });


        if (
            shutdownRequested_
            && userMessages_.empty())
        {
            return std::nullopt;
        }


        std::string message =
            std::move(
                userMessages_.front());


        userMessages_.pop_front();


        return message;
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