#pragma once

#include "persistence/IConversationStore.h"

#include <filesystem>

namespace rose::persistence
{

    // -------------------------------------------------------------------------
    // FileConversationStore
    // -------------------------------------------------------------------------
    //
    // Small append-only conversation journal used by the early Rose MVP.
    //
    // Goals:
    //
    //     local-first
    //     no external dependency
    //     append after every successful turn
    //     tolerate an incomplete final record after a process crash
    //     version the on-disk format
    //
    // This is NOT intended to become Rose's ultimate memory database.
    //
    // The IConversationStore boundary allows us to replace it later with
    // SQLite without changing RoseCore.
    class FileConversationStore final
        : public IConversationStore
    {
    public:
        explicit FileConversationStore(
            std::filesystem::path path);


        [[nodiscard]]
        std::vector<StoredConversationTurn>
            loadTurns() override;


        void appendTurn(
            std::string_view userText,
            std::string_view assistantText) override;


        void clear() override;


        [[nodiscard]]
        const std::filesystem::path& path() const noexcept;


    private:
        std::filesystem::path path_;
    };

} // namespace rose::persistence