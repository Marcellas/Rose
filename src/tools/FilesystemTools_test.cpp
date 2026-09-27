#include "tools/BatchMovePathsTool.h"
#include "tools/CreateDirectoryTool.h"
#include "tools/MovePathTool.h"
#include "tools/RecyclePathTool.h"
#include "tools/ScanDirectoryTreeTool.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace
{
    void require(
        const bool condition,
        const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }


    std::filesystem::path makeTempRoot()
    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / "rose-filesystem-tools-test";

        std::error_code error;
        std::filesystem::remove_all(root, error);
        error.clear();
        std::filesystem::create_directories(root, error);

        if (error)
        {
            throw std::system_error{
                error,
                "Could not create Rose filesystem test root"
            };
        }

        return root;
    }
}


int main()
{
    try
    {
        const std::filesystem::path root =
            makeTempRoot();

        rose::tools::CreateDirectoryTool createDirectory;
        require(
            createDirectory.descriptor().risk
                == rose::tools::ToolRisk::LocalWrite,
            "create_directory must be a LocalWrite tool");
        require(
            createDirectory.descriptor().consent
                == rose::tools::ToolConsent::RequiresConfirmation,
            "create_directory must require confirmation");

        const std::filesystem::path created =
            root / "organized";

        const rose::tools::ToolResult createResult =
            createDirectory.execute(
                rose::tools::ToolRequest{
                    .toolId = "create_directory",
                    .arguments = {
                        { "path", created.string() }
                    }
                });

        require(createResult.success, "create_directory should succeed");
        require(
            std::filesystem::is_directory(created),
            "create_directory should create the requested folder");

        const std::filesystem::path source =
            root / "sample.txt";

        {
            std::ofstream stream{ source };
            stream << "Rose filesystem test\n";
        }

        rose::tools::MovePathTool movePath;
        require(
            movePath.descriptor().consent
                == rose::tools::ToolConsent::RequiresConfirmation,
            "move_path must require confirmation");

        const std::filesystem::path moved =
            created / "renamed.txt";

        const rose::tools::ToolResult moveResult =
            movePath.execute(
                rose::tools::ToolRequest{
                    .toolId = "move_path",
                    .arguments = {
                        { "source", source.string() },
                        { "destination", moved.string() }
                    }
                });

        require(moveResult.success, "move_path should succeed");
        require(!std::filesystem::exists(source), "source should be moved");
        require(std::filesystem::exists(moved), "destination should exist");

        rose::tools::ScanDirectoryTreeTool scanner{
            rose::tools::ScanDirectoryTreeToolConfig{
                .maximumEntries = 100,
                .maximumOutputBytes = 16u * 1024u,
                .maximumDepth = 4
            }
        };

        const rose::tools::ToolResult scanResult =
            scanner.execute(
                rose::tools::ToolRequest{
                    .toolId = "scan_directory_tree",
                    .arguments = {
                        { "path", root.string() }
                    }
                });

        require(scanResult.success, "scan_directory_tree should succeed");
        require(
            scanResult.message.find("organized") != std::string::npos,
            "scan should contain the directory name");
        require(
            scanResult.message.find("renamed.txt") != std::string::npos,
            "scan should contain the moved file");


        const std::filesystem::path batchA =
            root / "batch-a.txt";

        const std::filesystem::path batchB =
            root / "batch-b.txt";

        {
            std::ofstream stream{ batchA };
            stream << "A\n";
        }

        {
            std::ofstream stream{ batchB };
            stream << "B\n";
        }

        const std::filesystem::path batchARenamed =
            root / "2026-09-15 A.txt";

        const std::filesystem::path batchBRenamed =
            root / "2026-09-15 B.txt";

        rose::tools::BatchMovePathsTool batchMove;

        require(
            batchMove.descriptor().risk
                == rose::tools::ToolRisk::LocalWrite,
            "batch_move_paths must be LocalWrite");

        require(
            batchMove.descriptor().consent
                == rose::tools::ToolConsent::RequiresConfirmation,
            "batch_move_paths must require confirmation");

        const rose::tools::ToolResult batchResult =
            batchMove.execute(
                rose::tools::ToolRequest{
                    .toolId = "batch_move_paths",
                    .arguments = {
                        {
                            "operations",
                            batchA.string()
                                + "=>"
                                + batchARenamed.string()
                                + "|"
                                + batchB.string()
                                + "=>"
                                + batchBRenamed.string()
                        }
                    }
                });

        require(batchResult.success, "batch_move_paths should succeed");
        require(!std::filesystem::exists(batchA), "batch source A should move");
        require(!std::filesystem::exists(batchB), "batch source B should move");
        require(std::filesystem::exists(batchARenamed), "batch destination A should exist");
        require(std::filesystem::exists(batchBRenamed), "batch destination B should exist");

        rose::tools::RecyclePathTool recyclePath;
        require(
            recyclePath.descriptor().risk
                == rose::tools::ToolRisk::Destructive,
            "recycle_path must be classified Destructive");
        require(
            recyclePath.descriptor().consent
                == rose::tools::ToolConsent::RequiresConfirmation,
            "recycle_path must require confirmation");

        std::error_code cleanupError;
        std::filesystem::remove_all(root, cleanupError);

        std::cout << "Rose FilesystemTools tests: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Rose FilesystemTools tests: FAIL: "
            << error.what()
            << '\n';
        return 1;
    }
}
