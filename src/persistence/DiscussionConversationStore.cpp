#include "persistence/DiscussionConversationStore.h"

#include "persistence/FileConversationStore.h"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace rose::persistence
{

    DiscussionConversationStore::DiscussionConversationStore(
        std::filesystem::path directory)
        : directory_{ std::move(directory) }
    {
        if (directory_.empty())
        {
            throw std::invalid_argument{
                "DiscussionConversationStore requires a directory."
            };
        }
    }


    DiscussionConversationStore::~DiscussionConversationStore() = default;


    void DiscussionConversationStore::selectDiscussion(
        std::string discussionId)
    {
        validateDiscussionId(discussionId);

        if (discussionId == activeDiscussionId_)
        {
            return;
        }

        std::filesystem::path path = directory_;
        path /= discussionId + ".rosechat";

        auto newStore =
            std::make_unique<FileConversationStore>(
                std::move(path));

        // Commit the new id/store together only after construction succeeds.
        activeDiscussionId_ = std::move(discussionId);
        activeStore_ = std::move(newStore);
    }


    bool DiscussionConversationStore::hasActiveDiscussion() const noexcept
    {
        return activeStore_ != nullptr;
    }


    const std::string&
        DiscussionConversationStore::activeDiscussionId() const noexcept
    {
        return activeDiscussionId_;
    }


    std::filesystem::path
        DiscussionConversationStore::activePath() const
    {
        if (!activeStore_)
        {
            throw std::logic_error{
                "Rose has no active discussion transcript."
            };
        }

        return activeStore_->path();
    }


    std::vector<StoredConversationTurn>
        DiscussionConversationStore::loadTurns()
    {
        if (!activeStore_)
        {
            return {};
        }

        return activeStore_->loadTurns();
    }


    void DiscussionConversationStore::appendTurn(
        const std::string_view userText,
        const std::string_view assistantText)
    {
        if (!activeStore_)
        {
            throw std::logic_error{
                "Rose cannot persist a turn without an active discussion."
            };
        }

        activeStore_->appendTurn(
            userText,
            assistantText);
    }


    void DiscussionConversationStore::clear()
    {
        if (!activeStore_)
        {
            return;
        }

        activeStore_->clear();
    }


    void DiscussionConversationStore::validateDiscussionId(
        const std::string_view discussionId)
    {
        if (discussionId.empty() || discussionId.size() > 128)
        {
            throw std::invalid_argument{
                "Discussion id has an invalid length."
            };
        }

        for (const unsigned char value : discussionId)
        {
            const bool allowed =
                std::isalnum(value) != 0
                || value == '-'
                || value == '_';

            if (!allowed)
            {
                throw std::invalid_argument{
                    "Discussion id contains an unsafe path character."
                };
            }
        }
    }

} // namespace rose::persistence
