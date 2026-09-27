#pragma once

#include "agent/AgentJournal.h"

#include <filesystem>

namespace rose::agent
{
    // Durable bounded backing store for AgentJournal snapshots.
    //
    // The journal remains the owner of retention policy. This class only performs
    // atomic-ish load/save of the already bounded event vector.
    class FileAgentJournalStore final
        : public IAgentJournalStore
    {
    public:
        explicit FileAgentJournalStore(
            std::filesystem::path path);

        [[nodiscard]]
        std::vector<AgentEvent> load() override;

        void save(
            const std::vector<AgentEvent>& events) override;

        [[nodiscard]]
        const std::filesystem::path& path() const noexcept;

    private:
        std::filesystem::path path_;
    };
}
