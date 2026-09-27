#include "archives/ZipArchiveService.h"
#include "files/FileFormatCatalog.h"
#include "tools/CreateZipArchiveTool.h"
#include "tools/ExtractZipArchiveTool.h"
#include "tools/ListZipArchiveTool.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(const bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }

    class FakeZipArchiveService final : public rose::archives::IZipArchiveService
    {
    public:
        [[nodiscard]]
        rose::archives::ZipArchiveListing list(
            const std::filesystem::path& archivePath) const override
        {
            lastList = archivePath;
            return rose::archives::ZipArchiveListing{
                .entries = {
                    rose::archives::ZipArchiveEntry{
                        .path = "docs/readme.txt",
                        .uncompressedBytes = 120,
                        .compressedBytes = 80,
                        .directory = false,
                        .safeRelativePath = true
                    },
                    rose::archives::ZipArchiveEntry{
                        .path = "../escape.txt",
                        .uncompressedBytes = 10,
                        .compressedBytes = 8,
                        .directory = false,
                        .safeRelativePath = false
                    }
                },
                .totalEntryCount = 2,
                .totalUncompressedBytes = 130,
                .truncated = false,
                .containsUnsafePaths = true
            };
        }

        void extract(
            const std::filesystem::path& archivePath,
            const std::filesystem::path& destinationDirectory) const override
        {
            lastExtractSource = archivePath;
            lastExtractDestination = destinationDirectory;
        }

        void create(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& destinationArchive) const override
        {
            lastCreateSource = sourcePath;
            lastCreateDestination = destinationArchive;
        }

        mutable std::filesystem::path lastList;
        mutable std::filesystem::path lastExtractSource;
        mutable std::filesystem::path lastExtractDestination;
        mutable std::filesystem::path lastCreateSource;
        mutable std::filesystem::path lastCreateDestination;
    };
}

int main()
{
    using rose::archives::isSafeZipEntryPath;

    require(isSafeZipEntryPath("docs/readme.txt"), "ordinary relative ZIP path should be safe");
    require(isSafeZipEntryPath("folder/sub/"), "ordinary directory ZIP path should be safe");
    require(!isSafeZipEntryPath("../escape.txt"), "parent traversal must be unsafe");
    require(!isSafeZipEntryPath("folder/../../escape.txt"), "nested parent traversal must be unsafe");
    require(!isSafeZipEntryPath("/absolute.txt"), "absolute slash path must be unsafe");
    require(!isSafeZipEntryPath("C:/absolute.txt"), "drive-qualified path must be unsafe");
    require(!isSafeZipEntryPath("folder/name:stream"), "ADS/colon path must be unsafe");

    const auto zipFormat = rose::files::classifyFileFormat("sample.zip");
    require(zipFormat.kind == rose::files::FileFormatKind::Archive,
            ".zip should be recognized as an archive");
    require(zipFormat.projectIndexable && zipFormat.attachmentReadable && zipFormat.agentReadable,
            ".zip should advertise the implemented manifest/tool capabilities");

    const auto sevenZipFormat = rose::files::classifyFileFormat("sample.7z");
    require(sevenZipFormat.kind == rose::files::FileFormatKind::Archive,
            ".7z should still be recognized as an archive family");
    require(!sevenZipFormat.agentReadable,
            "unsupported archive families must remain fail-closed");

    FakeZipArchiveService service;
    rose::tools::ListZipArchiveTool listTool{ service };
    rose::tools::ExtractZipArchiveTool extractTool{ service };
    rose::tools::CreateZipArchiveTool createTool{ service };

#ifdef _WIN32
    const std::string archive = "C:\\Data\\bundle.zip";
    const std::string destination = "C:\\Data\\unpacked";
    const std::string source = "C:\\Data\\Project";
    const std::string created = "C:\\Data\\Project.zip";
#else
    const std::string archive = "/tmp/bundle.zip";
    const std::string destination = "/tmp/unpacked";
    const std::string source = "/tmp/Project";
    const std::string created = "/tmp/Project.zip";
#endif

    const auto listResult = listTool.execute(
        rose::tools::ToolRequest{
            .toolId = "list_zip_archive",
            .arguments = { { "path", archive } }
        });
    require(listResult.success, "ZIP listing tool should succeed through service boundary");
    require(listResult.message.find("docs/readme.txt") != std::string::npos,
            "ZIP listing should expose safe member names");
    require(listResult.message.find("[UNSAFE] ../escape.txt") != std::string::npos,
            "ZIP listing should visibly mark unsafe member names");
    require(listTool.descriptor().risk == rose::tools::ToolRisk::ReadOnly,
            "ZIP listing must remain read-only");

    const auto extractResult = extractTool.execute(
        rose::tools::ToolRequest{
            .toolId = "extract_zip_archive",
            .arguments = {
                { "path", archive },
                { "destination", destination }
            }
        });
    require(extractResult.success, "ZIP extraction tool should delegate exact paths");
    require(service.lastExtractSource == std::filesystem::path{ archive },
            "ZIP extraction should preserve source path");
    require(service.lastExtractDestination == std::filesystem::path{ destination },
            "ZIP extraction should preserve destination path");
    require(extractTool.descriptor().risk == rose::tools::ToolRisk::LocalWrite,
            "ZIP extraction must be classified as local write");
    require(extractTool.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation,
            "ZIP extraction must require confirmation");

    const auto createResult = createTool.execute(
        rose::tools::ToolRequest{
            .toolId = "create_zip_archive",
            .arguments = {
                { "source", source },
                { "destination", created }
            }
        });
    require(createResult.success, "ZIP creation tool should delegate exact paths");
    require(service.lastCreateSource == std::filesystem::path{ source },
            "ZIP creation should preserve source path");
    require(service.lastCreateDestination == std::filesystem::path{ created },
            "ZIP creation should preserve destination path");
    require(createTool.descriptor().risk == rose::tools::ToolRisk::LocalWrite,
            "ZIP creation must be classified as local write");
    require(createTool.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation,
            "ZIP creation must require confirmation");

    std::cout << "Rose ZIP archive tools tests: PASS\n";
    return 0;
}
