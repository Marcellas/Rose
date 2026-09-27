#include "files/FileFormatCatalog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace rose::files
{
    namespace
    {
        [[nodiscard]]
        std::string lowerExtension(
            const std::filesystem::path& path)
        {
            std::string extension = path.extension().string();
            std::transform(
                extension.begin(),
                extension.end(),
                extension.begin(),
                [](const unsigned char value)
                {
                    return static_cast<char>(std::tolower(value));
                });
            return extension;
        }

        template<std::size_t Size>
        [[nodiscard]]
        bool in(
            const std::string_view value,
            const std::array<std::string_view, Size>& values) noexcept
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }
    }

    FileFormatInfo classifyFileFormat(
        const std::filesystem::path& path) noexcept
    {
        const std::string extension = lowerExtension(path);

        static constexpr std::array<std::string_view, 1> pdf{ ".pdf" };
        static constexpr std::array<std::string_view, 7> images{
            ".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"
        };
        static constexpr std::array<std::string_view, 1> animatedImages{ ".gif" };
        static constexpr std::array<std::string_view, 2> word{
            ".docx", ".docm"
        };
        static constexpr std::array<std::string_view, 2> spreadsheets{
            ".xlsx", ".xlsm"
        };
        static constexpr std::array<std::string_view, 2> presentations{
            ".pptx", ".pptm"
        };
        static constexpr std::array<std::string_view, 6> legacyOffice{
            ".doc", ".dot", ".xls", ".xlt", ".ppt", ".pps"
        };
        static constexpr std::array<std::string_view, 1> zipArchives{ ".zip" };
        static constexpr std::array<std::string_view, 9> otherArchives{
            ".7z", ".rar", ".tar", ".gz", ".tgz", ".bz2", ".xz", ".cab", ".iso"
        };
        static constexpr std::array<std::string_view, 9> videos{
            ".webm", ".mp4", ".m4v", ".mov", ".mkv", ".avi", ".wmv", ".mpeg", ".mpg"
        };
        static constexpr std::array<std::string_view, 8> audio{
            ".mp3", ".wav", ".flac", ".ogg", ".m4a", ".aac", ".wma", ".opus"
        };
        static constexpr std::array<std::string_view, 8> databases{
            ".sqlite", ".sqlite3", ".db", ".mdb", ".accdb", ".db3", ".sqlitedb", ".duckdb"
        };
        static constexpr std::array<std::string_view, 2> shortcuts{
            ".lnk", ".url"
        };
        static constexpr std::array<std::string_view, 4> executables{
            ".exe", ".com", ".bat", ".cmd"
        };

        if (in(extension, pdf))
        {
            return { FileFormatKind::Pdf, "PDF", true, true, true };
        }
        if (in(extension, images))
        {
            return { FileFormatKind::Image, "image", true, true, true };
        }
        if (in(extension, animatedImages))
        {
            return { FileFormatKind::AnimatedImage, "animated image", true, true, true };
        }
        if (in(extension, word))
        {
            return { FileFormatKind::OfficeWordOpenXml, "Word/OpenXML", true, true, true };
        }
        if (in(extension, spreadsheets))
        {
            return { FileFormatKind::OfficeSpreadsheetOpenXml, "Excel/OpenXML", true, true, true };
        }
        if (in(extension, presentations))
        {
            return { FileFormatKind::OfficePresentationOpenXml, "PowerPoint/OpenXML", true, true, true };
        }
        if (in(extension, legacyOffice))
        {
            return { FileFormatKind::OfficeLegacy, "legacy Microsoft Office", false, false, false };
        }
        if (in(extension, zipArchives))
        {
            return { FileFormatKind::Archive, "ZIP archive", true, true, true };
        }
        if (in(extension, otherArchives))
        {
            return { FileFormatKind::Archive, "archive", false, false, false };
        }
        if (in(extension, videos))
        {
            return { FileFormatKind::Video, "video", true, true, true };
        }
        if (in(extension, audio))
        {
            return { FileFormatKind::Audio, "audio", false, false, false };
        }
        if (in(extension, databases))
        {
            return { FileFormatKind::Database, "database", true, true, true };
        }
        if (in(extension, shortcuts))
        {
            return { FileFormatKind::Shortcut, "shortcut", true, true, true };
        }
        if (in(extension, executables))
        {
            return { FileFormatKind::Executable, "executable/program", false, false, false };
        }

        return {};
    }

    bool isOpenXmlOfficeFile(
        const std::filesystem::path& path) noexcept
    {
        const auto kind = classifyFileFormat(path).kind;
        return kind == FileFormatKind::OfficeWordOpenXml
            || kind == FileFormatKind::OfficeSpreadsheetOpenXml
            || kind == FileFormatKind::OfficePresentationOpenXml;
    }

    bool isImageFile(
        const std::filesystem::path& path) noexcept
    {
        return classifyFileFormat(path).kind == FileFormatKind::Image;
    }

    bool isMediaFile(
        const std::filesystem::path& path) noexcept
    {
        const auto kind = classifyFileFormat(path).kind;
        return kind == FileFormatKind::Video
            || kind == FileFormatKind::AnimatedImage;
    }

    bool isPdfFile(
        const std::filesystem::path& path) noexcept
    {
        return classifyFileFormat(path).kind == FileFormatKind::Pdf;
    }

    bool isZipArchiveFile(
        const std::filesystem::path& path) noexcept
    {
        const std::string extension = lowerExtension(path);
        return extension == ".zip";
    }
}
