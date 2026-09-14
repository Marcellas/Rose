#pragma once

#include "artifacts/Artifact.h"

#include <filesystem>
#include <string_view>

namespace rose::artifacts
{

    // Rose-owned persistent output directory.
    //
    // The store allocates unique paths under one configured root. Tools are given
    // only those paths, which prevents a generator from choosing an arbitrary user
    // destination. Later, "Save As" can be a separate permissioned copy operation.
    class ArtifactStore final
    {
    public:
        explicit ArtifactStore(
            std::filesystem::path rootDirectory);

        [[nodiscard]]
        const std::filesystem::path& rootDirectory() const noexcept;

        [[nodiscard]]
        std::filesystem::path allocatePath(
            std::string_view suggestedStem,
            std::string_view extension) const;

        [[nodiscard]]
        Artifact finalize(
            const std::filesystem::path& path,
            std::string_view displayName,
            std::string_view mediaType,
            ArtifactKind kind) const;

    private:
        std::filesystem::path rootDirectory_;
    };

} // namespace rose::artifacts
