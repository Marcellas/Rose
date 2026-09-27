#include "integrations/WindowsCredentialVault.h"

#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wincred.h>
#endif

namespace rose::integrations
{
    namespace
    {
        void validateLogicalKey(const std::string_view logicalKey)
        {
            if (logicalKey.empty() || logicalKey.size() > 200)
            {
                throw std::invalid_argument{ "Credential logical key must contain 1-200 characters." };
            }

            for (const char character : logicalKey)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (value < 0x20 || value == 0x7f)
                {
                    throw std::invalid_argument{ "Credential logical key may not contain control characters." };
                }
            }
        }

#if defined(_WIN32)
        std::wstring utf8ToWide(const std::string_view text)
        {
            if (text.empty()) return {};
            if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                throw std::invalid_argument{ "Credential key is too large." };
            }

            const int inputSize = static_cast<int>(text.size());
            const int required = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                text.data(),
                inputSize,
                nullptr,
                0);

            if (required <= 0)
            {
                throw std::runtime_error{ "Credential key is not valid UTF-8." };
            }

            std::wstring result(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    text.data(),
                    inputSize,
                    result.data(),
                    required) != required)
            {
                throw std::runtime_error{ "Could not convert Rose credential key to UTF-16." };
            }
            return result;
        }

        [[noreturn]] void throwWindowsError(const char* operation)
        {
            throw std::runtime_error{
                std::string{ operation }
                + " failed with Windows error "
                + std::to_string(GetLastError())
                + "."
            };
        }
#endif
    }

    WindowsCredentialVault::WindowsCredentialVault(std::string applicationPrefix)
        : applicationPrefix_{ std::move(applicationPrefix) }
    {
        if (applicationPrefix_.empty())
        {
            throw std::invalid_argument{ "WindowsCredentialVault requires an application prefix." };
        }
    }

    bool WindowsCredentialVault::available() const noexcept
    {
#if defined(_WIN32)
        return true;
#else
        return false;
#endif
    }

    std::string WindowsCredentialVault::backendName() const
    {
#if defined(_WIN32)
        return "Windows Credential Manager";
#else
        return "Unavailable on this platform";
#endif
    }

    void WindowsCredentialVault::storeSecret(
        const std::string_view logicalKey,
        const std::string_view secret)
    {
#if defined(_WIN32)
        if (secret.empty())
        {
            throw std::invalid_argument{ "Credential secret may not be empty." };
        }

        const std::string targetUtf8 = qualifiedKey(logicalKey);
        const std::wstring target = utf8ToWide(targetUtf8);
        const std::wstring userName = L"Rose";

        if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
        {
            throw std::invalid_argument{ "Secret exceeds Windows Credential Manager's generic credential size limit." };
        }

        std::vector<BYTE> secretBytes(secret.size());
        std::memcpy(secretBytes.data(), secret.data(), secret.size());
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(target.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(secretBytes.size());
        credential.CredentialBlob = secretBytes.empty() ? nullptr : secretBytes.data();
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = const_cast<LPWSTR>(userName.c_str());

        const BOOL written = CredWriteW(&credential, 0);
        if (!secretBytes.empty())
        {
            SecureZeroMemory(secretBytes.data(), secretBytes.size());
        }
        if (!written)
        {
            throwWindowsError("CredWriteW");
        }
#else
        (void)logicalKey;
        (void)secret;
        throw std::runtime_error{ "Windows Credential Manager is unavailable on this platform." };
#endif
    }

    std::optional<std::string> WindowsCredentialVault::readSecret(
        const std::string_view logicalKey) const
    {
#if defined(_WIN32)
        const std::wstring target = utf8ToWide(qualifiedKey(logicalKey));
        PCREDENTIALW rawCredential{ nullptr };
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &rawCredential))
        {
            if (GetLastError() == ERROR_NOT_FOUND)
            {
                return std::nullopt;
            }
            throwWindowsError("CredReadW");
        }

        struct CredentialDeleter
        {
            void operator()(CREDENTIALW* value) const noexcept
            {
                if (value == nullptr) return;
                if (value->CredentialBlob != nullptr && value->CredentialBlobSize > 0)
                {
                    SecureZeroMemory(value->CredentialBlob, value->CredentialBlobSize);
                }
                CredFree(value);
            }
        };

        std::unique_ptr<CREDENTIALW, CredentialDeleter> credential{ rawCredential };
        const char* bytes = reinterpret_cast<const char*>(credential->CredentialBlob);
        return std::string{
            bytes == nullptr ? "" : bytes,
            static_cast<std::size_t>(credential->CredentialBlobSize)
        };
#else
        (void)logicalKey;
        throw std::runtime_error{ "Windows Credential Manager is unavailable on this platform." };
#endif
    }

    bool WindowsCredentialVault::eraseSecret(const std::string_view logicalKey)
    {
#if defined(_WIN32)
        const std::wstring target = utf8ToWide(qualifiedKey(logicalKey));
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
        {
            return true;
        }
        if (GetLastError() == ERROR_NOT_FOUND)
        {
            return false;
        }
        throwWindowsError("CredDeleteW");
#else
        (void)logicalKey;
        throw std::runtime_error{ "Windows Credential Manager is unavailable on this platform." };
#endif
    }

    std::string WindowsCredentialVault::qualifiedKey(const std::string_view logicalKey) const
    {
        validateLogicalKey(logicalKey);
        return applicationPrefix_ + "/" + std::string{ logicalKey };
    }
}
