#pragma once

#include "integrations/IntegrationTypes.h"

namespace rose::integrations
{
    // Persistence boundary for Rose's local integration authorization settings.
    // Provider tokens/secrets NEVER belong in this store; those go through
    // ICredentialVault instead.
    class IIntegrationPermissionStore
    {
    public:
        virtual ~IIntegrationPermissionStore() = default;

        [[nodiscard]] virtual IntegrationPermissionSnapshot load() = 0;
        virtual void save(const IntegrationPermissionSnapshot& snapshot) = 0;
    };
}
