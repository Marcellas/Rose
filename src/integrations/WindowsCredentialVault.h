#pragma once

#include "integrations/ICredentialVault.h"

#include <string>

namespace rose::integrations
{
    // Windows implementation backed by Credential Manager generic credentials.
    // Secret material is never written into Rose's config/persistence files.
    class WindowsCredentialVault final
        : public ICredentialVault
    {
    public:
        explicit WindowsCredentialVault(std::string applicationPrefix = "Rose");

        [[nodiscard]] bool available() const noexcept override;
        [[nodiscard]] std::string backendName() const override;

        void storeSecret(
            std::string_view logicalKey,
            std::string_view secret) override;

        [[nodiscard]] std::optional<std::string> readSecret(
            std::string_view logicalKey) const override;

        [[nodiscard]] bool eraseSecret(
            std::string_view logicalKey) override;

    private:
        [[nodiscard]] std::string qualifiedKey(std::string_view logicalKey) const;

        std::string applicationPrefix_;
    };
}
