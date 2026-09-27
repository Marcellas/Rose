#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rose::integrations
{
    // Secure secret-storage boundary. Implementations own OS-specific credential
    // APIs; callers see only logical Rose keys and opaque secret bytes/UTF-8 text.
    class ICredentialVault
    {
    public:
        virtual ~ICredentialVault() = default;

        [[nodiscard]] virtual bool available() const noexcept = 0;
        [[nodiscard]] virtual std::string backendName() const = 0;

        virtual void storeSecret(
            std::string_view logicalKey,
            std::string_view secret) = 0;

        [[nodiscard]] virtual std::optional<std::string> readSecret(
            std::string_view logicalKey) const = 0;

        [[nodiscard]] virtual bool eraseSecret(
            std::string_view logicalKey) = 0;
    };
}
