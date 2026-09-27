#pragma once

#include <filesystem>
#include <string>

namespace rose::shortcuts
{
    struct ShortcutInspection
    {
        std::string kind;
        std::string target;
        std::string arguments;
        std::string workingDirectory;
        std::string description;
        std::string iconLocation;
        std::string url;
        bool targetExists{ false };
    };

    class IShortcutService
    {
    public:
        virtual ~IShortcutService() = default;
        [[nodiscard]] virtual ShortcutInspection inspect(const std::filesystem::path& path) const = 0;
    };

    // Read-only Windows shortcut inspector. .url files are portable text metadata;
    // .lnk files use the Windows Shell Link COM API and are never launched.
    class LocalShortcutService final : public IShortcutService
    {
    public:
        [[nodiscard]] ShortcutInspection inspect(const std::filesystem::path& path) const override;
    };
}
