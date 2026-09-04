#include "conversation/Conversation.h"

#include <stdexcept>
#include <utility>

namespace rose::conversation
{

    Conversation::Conversation(
        const ConversationConfig config)
        : config_{ config }
    {
        // We currently store conversational turns as:
        //
        //     User
        //     Assistant
        //
        // A working window smaller than two messages is therefore not useful
        // for our current conversation model.
        if (config_.maxWorkingMessages < 2)
        {
            throw std::invalid_argument{
                "Conversation maxWorkingMessages must be at least 2."
            };
        }
    }


    void Conversation::commitTurn(
        std::string userText,
        std::string assistantText)
    {
        // Reserve space for BOTH messages before modifying the vector.
        //
        // WHY:
        //
        // If memory allocation fails, reserve() throws before either message
        // has been inserted. This avoids leaving Conversation with half of a
        // completed turn.
        //
        // After reserve() succeeds, moving the strings into the vector does
        // not require another vector allocation.
        messages_.reserve(
            messages_.size() + 2);


        messages_.push_back(
            model::ModelMessage{
                .role = model::ModelRole::User,
                .content = std::move(userText)
            });


        messages_.push_back(
            model::ModelMessage{
                .role = model::ModelRole::Assistant,
                .content = std::move(assistantText)
            });
    }


    std::vector<model::ModelMessage>
        Conversation::buildWorkingContext() const
    {
        const std::size_t startIndex =
            workingStartIndex();


        std::vector<model::ModelMessage> context;

        context.reserve(
            messages_.size() - startIndex);


        // We deliberately copy the messages into a request-owned vector.
        //
        // This means a future background inference task will not depend on the
        // Conversation object's vector storage remaining unchanged while the
        // model is working.
        for (
            std::size_t index = startIndex;
            index < messages_.size();
            ++index)
        {
            context.push_back(
                messages_[index]);
        }


        return context;
    }


    void Conversation::clear() noexcept
    {
        // clear() destroys stored messages but normally retains vector capacity.
        //
        // This is useful for an interactive program because another
        // conversation can reuse that allocation instead of immediately asking
        // the heap for more memory.
        messages_.clear();
    }


    std::size_t
        Conversation::storedMessageCount() const noexcept
    {
        return messages_.size();
    }


    std::size_t
        Conversation::workingStartIndex() const noexcept
    {
        if (
            messages_.size()
            <= config_.maxWorkingMessages)
        {
            return 0;
        }


        std::size_t startIndex =
            messages_.size()
            - config_.maxWorkingMessages;


        // Avoid starting the supplied history with an Assistant response.
        //
        // Example:
        //
        //     discarded: User
        //     retained:  Assistant   <- bad beginning
        //                User
        //                Assistant
        //
        // Advance until the beginning of the next user turn:
        //
        //     retained:  User
        //                Assistant
        while (
            startIndex < messages_.size()
            && messages_[startIndex].role
            != model::ModelRole::User)
        {
            ++startIndex;
        }


        // commitTurn() currently guarantees User/Assistant pairs, so normally
        // we'll always find another User message.
        //
        // Returning zero is a defensive fallback in case future message types
        // change that invariant.
        if (startIndex >= messages_.size())
        {
            return 0;
        }


        return startIndex;
    }

} // namespace rose::conversation