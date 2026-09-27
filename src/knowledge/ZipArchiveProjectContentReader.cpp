#include "knowledge/ZipArchiveProjectContentReader.h"

#include "files/FileFormatCatalog.h"

#include <sstream>
#include <stdexcept>
#include <string>

namespace rose::knowledge
{
    ZipArchiveProjectContentReader::ZipArchiveProjectContentReader(
        const ZipArchiveProjectContentReaderConfig config)
        : config_{ config }
        , archiveService_{}
    {
        if (config_.maximumManifestBytes == 0)
        {
            throw std::invalid_argument{
                "ZipArchiveProjectContentReader maximumManifestBytes must be greater than zero."
            };
        }
    }

    std::string_view ZipArchiveProjectContentReader::id() const noexcept
    {
        return "zip-manifest";
    }

    std::uintmax_t ZipArchiveProjectContentReader::maximumSourceBytes() const noexcept
    {
        // Manifest listing streams archive metadata rather than loading the entire
        // compressed source into model memory. Keep a separate hard ceiling to
        // avoid unbounded background work while allowing realistic project ZIPs.
        return 8ull * 1024ull * 1024ull * 1024ull;
    }

    bool ZipArchiveProjectContentReader::supports(
        const std::filesystem::path& path) const noexcept
    {
        return files::isZipArchiveFile(path);
    }

    ExtractedProjectContent ZipArchiveProjectContentReader::read(
        const std::filesystem::path& path,
        const std::uintmax_t sourceBytes) const
    {
        (void)sourceBytes;
        const archives::ZipArchiveListing listing = archiveService_.list(path);

        std::ostringstream text;
        text
            << "ZIP archive manifest\n"
            << "entries=" << listing.totalEntryCount << '\n'
            << "total_uncompressed_bytes=" << listing.totalUncompressedBytes << '\n'
            << "manifest_truncated=" << (listing.truncated ? "true" : "false") << '\n'
            << "contains_unsafe_paths=" << (listing.containsUnsafePaths ? "true" : "false") << '\n';

        bool byteTruncated{ false };
        for (const auto& entry : listing.entries)
        {
            std::ostringstream line;
            line
                << (entry.directory ? "directory " : "file ")
                << (entry.safeRelativePath ? "" : "UNSAFE_PATH ")
                << entry.path;
            if (!entry.directory)
            {
                line << " bytes=" << entry.uncompressedBytes;
            }
            line << '\n';
            const std::string value = line.str();
            if (text.tellp() >= 0
                && static_cast<std::size_t>(text.tellp()) + value.size()
                    > config_.maximumManifestBytes)
            {
                byteTruncated = true;
                break;
            }
            text << value;
        }
        if (byteTruncated)
        {
            text << "Rose truncated the indexed ZIP manifest.\n";
        }

        return ExtractedProjectContent{
            .contentKind = "archive/zip-manifest",
            .readerId = std::string{ id() },
            .segments = {
                ProjectContentSegment{
                    .locator = "archive=manifest",
                    .text = text.str()
                }
            }
        };
    }
}
