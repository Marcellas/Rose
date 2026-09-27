#pragma once

#include "integrations/IIntegrationPermissionStore.h"

#include <string>
#include <string_view>

namespace rose::integrations
{
    // Owns Rose's local authorization policy for optional external integrations.
    // This is an additional gate, not a replacement for provider OAuth scopes or
    // per-action ToolExecutionPolicy confirmation.
    class IntegrationPermissionRepository final
    {
    public:
        explicit IntegrationPermissionRepository(IIntegrationPermissionStore& store);

        [[nodiscard]] const IntegrationPermissionSnapshot& snapshot() const noexcept;
        [[nodiscard]] bool isAllowed(
            std::string_view integrationId,
            IntegrationCapability capability) const noexcept;

        void setAllowed(
            std::string_view integrationId,
            IntegrationCapability capability,
            bool allowed);

    private:
        [[nodiscard]] static std::string normalizeIntegrationId(
            std::string_view integrationId);

        [[nodiscard]] IntegrationPermissionRecord* findMutable(
            std::string_view normalizedId) noexcept;

        [[nodiscard]] const IntegrationPermissionRecord* find(
            std::string_view normalizedId) const noexcept;

        void validateLoadedSnapshot();
        void persist();

        IIntegrationPermissionStore& store_;
        IntegrationPermissionSnapshot snapshot_;
    };
}
