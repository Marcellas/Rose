#include "permissions/PermissionSystem.h"

#include <system_error>

namespace rose::permissions
{

    std::string PermissionSystem::normalizedKey(
        const std::filesystem::path& path)
    {
        std::error_code error;

        // weakly_canonical resolves aliases/symlinks for the portions that exist,
        // which gives a stronger equality check than comparing user spelling alone.
        std::filesystem::path normalized =
            std::filesystem::weakly_canonical(
                path,
                error);

        if (error)
        {
            error.clear();

            normalized =
                std::filesystem::absolute(
                    path,
                    error);

            if (error)
            {
                normalized = path;
            }
        }

        return normalized.lexically_normal().string();
    }


    void PermissionSystem::grantReadOnce(
        const std::filesystem::path& path)
    {
        readOnce_.insert(
            normalizedKey(path));
    }


    bool PermissionSystem::consumeReadOnce(
        const std::filesystem::path& path)
    {
        const std::string key =
            normalizedKey(path);

        const auto found =
            readOnce_.find(key);

        if (found == readOnce_.end())
        {
            return false;
        }

        readOnce_.erase(found);
        return true;
    }


    void PermissionSystem::clearTemporaryGrants() noexcept
    {
        readOnce_.clear();
    }

} // namespace rose::permissions
