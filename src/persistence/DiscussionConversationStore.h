#pragma once

#include "persistence/IConversationStore.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace rose::persistence
{
    class FileConversationStore;

    // Adapter that gives the existing RoseCore/IConversationStore path one active
    // discussion at a time while keeping each transcript in its own local file.
    //
    // This object owns only the currently selected FileConversationStore. RoseCore
    // may continue borrowing this stable adapter across discussion switches.
    class DiscussionConversationStore final
        : public IConversationStore
    {
    public:
        explicit DiscussionConversationStore(
            std::filesystem::path directory);

        ~DiscussionConversationStore() override;

        void selectDiscussion(
            std::string discussionId);

        [[nodiscard]]
        bool hasActiveDiscussion() const noexcept;

        [[nodiscard]]
        const std::string& activeDiscussionId() const noexcept;

        [[nodiscard]]
        std::filesystem::path activePath() const;

        [[nodiscard]]
        std::vector<StoredConversationTurn>
            loadTurns() override;

        void appendTurn(
            std::string_view userText,
            std::string_view assistantText) override;

        void clear() override;

    private:
        static void validateDiscussionId(
            std::string_view discussionId);

        std::filesystem::path directory_;
        std::string activeDiscussionId_;
        std::unique_ptr<FileConversationStore> activeStore_;
    };

} // namespace rose::persistence
