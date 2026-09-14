#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>

namespace rose::permissions
{

    // -----------------------------------------------------------------------------
    // PermissionSystem
    // -----------------------------------------------------------------------------
    //
    // First MVP permission layer.
    //
    // A file dropped onto Rose grants READ access to exactly that file, exactly once.
    // No parent directory, sibling file, write, move, rename, or delete permission is
    // implied by the grant.
    //
    // This class is intentionally worker-thread-owned and currently needs no mutex.
    class PermissionSystem final
    {
    public:
        void grantReadOnce(
            const std::filesystem::path& path);

        // Consume a one-shot grant. Returns false when no matching grant exists.
        [[nodiscard]]
        bool consumeReadOnce(
            const std::filesystem::path& path);

        void clearTemporaryGrants() noexcept;

    private:
        [[nodiscard]]
        static std::string normalizedKey(
            const std::filesystem::path& path);

        std::unordered_set<std::string> readOnce_;
    };

} // namespace rose::permissions
