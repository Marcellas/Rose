#pragma once

#include <filesystem>
#include <string_view>

namespace rose::files
{
    enum class FileFormatKind
    {
        Unknown,
        TextSource,
        Pdf,
        Image,
        AnimatedImage,
        OfficeWordOpenXml,
        OfficeSpreadsheetOpenXml,
        OfficePresentationOpenXml,
        OfficeLegacy,
        Archive,
        Video,
        Audio,
        Database,
        Shortcut,
        Executable
    };

    struct FileFormatInfo
    {
        FileFormatKind kind{ FileFormatKind::Unknown };
        std::string_view family;
        bool projectIndexable{ false };
        bool attachmentReadable{ false };
        bool agentReadable{ false };
    };

    [[nodiscard]]
    FileFormatInfo classifyFileFormat(
        const std::filesystem::path& path) noexcept;

    [[nodiscard]]
    bool isOpenXmlOfficeFile(
        const std::filesystem::path& path) noexcept;

    [[nodiscard]]
    bool isImageFile(
        const std::filesystem::path& path) noexcept;

    [[nodiscard]]
    bool isMediaFile(
        const std::filesystem::path& path) noexcept;

    [[nodiscard]]
    bool isPdfFile(
        const std::filesystem::path& path) noexcept;

    [[nodiscard]]
    bool isZipArchiveFile(
        const std::filesystem::path& path) noexcept;
}
