#pragma once

#include "knowledge/IProjectKnowledgeStore.h"

#include <filesystem>

namespace rose::knowledge
{
    // Durable local store for extracted/chunked project source material.
    //
    // The format is intentionally Rose-owned and provider-neutral. It contains
    // extracted text plus source provenance only; model state and embeddings never
    // live here. Writes use a temporary sibling followed by rename so a crash does
    // not normally replace a good index with a partially written one.
    class FileProjectKnowledgeStore final : public IProjectKnowledgeStore
    {
    public:
        explicit FileProjectKnowledgeStore(
            std::filesystem::path path);

        [[nodiscard]]
        ProjectKnowledgeSnapshot load() override;

        void save(
            const ProjectKnowledgeSnapshot& snapshot) override;

        [[nodiscard]]
        const std::filesystem::path& path() const noexcept;

    private:
        std::filesystem::path path_;
    };
}
