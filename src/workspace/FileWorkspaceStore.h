#pragma once

#include "workspace/IWorkspaceStore.h"

#include <filesystem>

namespace rose::workspace
{

    // Local-first metadata store for Rose Projects and Discussions.
    //
    // The format is deliberately small, versioned, bounded, and independent of
    // any model provider. save() rewrites one compact snapshot through a temporary
    // sibling file so normal operation never partially overwrites the live file.
    class FileWorkspaceStore final
        : public IWorkspaceStore
    {
    public:
        explicit FileWorkspaceStore(
            std::filesystem::path path);

        [[nodiscard]]
        WorkspaceSnapshot load() override;

        void save(
            const WorkspaceSnapshot& snapshot) override;

        [[nodiscard]]
        const std::filesystem::path& path() const noexcept;

    private:
        std::filesystem::path path_;
    };

} // namespace rose::workspace
