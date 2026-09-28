#include "tools/BatchMovePathsTool.h"
#include "tools/CreateDirectoryTool.h"
#include "tools/MovePathTool.h"
#include "tools/RecyclePathTool.h"
#include "permissions/PermissionSystem.h"
#include "files/SourceWindowDigest.h"
#include "tools/ReadFileTool.h"
#include "tools/ReadTextFileRegisteredTool.h"
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
        require(
            scanResult.message.find(
                "absolute_path=" + moved.lexically_normal().string())
                != std::string::npos,
            "scan should expose Rose-grounded absolute child paths for safe follow-up");


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

        // ------------------------------------------------------------------
        // Exact UTF-8 source windows for compiler/test diagnostics
        // ------------------------------------------------------------------
        const std::filesystem::path largeSource =
            root / "large-source.cpp";

        {
            std::ofstream stream{
                largeSource,
                std::ios::binary
            };

            for (int line = 1; line <= 3200; ++line)
            {
                stream
                    << "// source line "
                    << line
                    << " keeps this fixture beyond the ordinary prefix reader boundary";

                if (line == 2500)
                {
                    stream << " DIAGNOSTIC_TARGET";
                }

                stream << "\r\n";
            }
        }

        rose::permissions::PermissionSystem readPermissions;
        rose::tools::ReadFileTool readFileTool{
            readPermissions,
            rose::tools::ReadFileConfig{
                .maximumTextBytes = 64u * 1024u,
                .maximumTextRangeScanBytes = 4u * 1024u * 1024u,
                .maximumBinaryBytes = 1024u * 1024u
            }
        };

        rose::tools::ReadTextFileRegisteredTool sourceReader{
            readPermissions,
            readFileTool,
            rose::tools::ReadTextFileRegisteredToolConfig{
                .maximumObservationBytes = 8u * 1024u,
                .defaultLineCount = 80,
                .maximumLineCount = 200
            }
        };

        require(
            sourceReader.descriptor().risk
                == rose::tools::ToolRisk::ReadOnly,
            "read_text_file must remain ReadOnly");
        require(
            sourceReader.descriptor().consent
                == rose::tools::ToolConsent::RequiresConfirmation,
            "read_text_file must remain confirmation-gated");
        require(
            sourceReader.descriptor().parameters.size() == 3,
            "read_text_file should expose path plus optional source-window parameters");

        const rose::tools::ToolResult diagnosticWindow =
            sourceReader.execute(
                rose::tools::ToolRequest{
                    .toolId = "read_text_file",
                    .arguments = {
                        { "path", largeSource.string() },
                        { "start_line", "2498" },
                        { "line_count", "5" }
                    }
                });

        require(
            diagnosticWindow.success,
            "read_text_file source window should succeed");
        require(
            diagnosticWindow.message.find("requested_start_line=2498")
                != std::string::npos,
            "source-window observation should retain its one-based start line");
        require(
            diagnosticWindow.message.find("DIAGNOSTIC_TARGET")
                != std::string::npos,
            "source-window read must reach diagnostic context beyond the ordinary 64 KiB prefix");
        require(
            diagnosticWindow.message.find("2498|// source line 2498")
                != std::string::npos
                && diagnosticWindow.message.find("2500|// source line 2500")
                    != std::string::npos,
            "source-window observations should expose absolute one-based line numbers without changing source content after the delimiter");
        require(
            diagnosticWindow.sourceWindowEvidence.has_value(),
            "a complete source-window read should produce typed patch provenance");
        require(
            diagnosticWindow.sourceWindowEvidence->startLine == 2498
                && diagnosticWindow.sourceWindowEvidence->lineCount == 5
                && diagnosticWindow.sourceWindowEvidence->lineSha256s.size() == 5
                && rose::files::isSourceWindowSha256(
                    diagnosticWindow.sourceWindowEvidence->sha256),
            "source-window provenance should retain the exact observed coordinates and SHA-256 digest");
        require(
            diagnosticWindow.trustedMetadata.find("producer_tool=read_text_file")
                != std::string::npos
                && diagnosticWindow.trustedMetadata.find("source_window_sha256=")
                    != std::string::npos,
            "source-window read should expose only structured digest metadata through the trusted routing channel");
        require(
            diagnosticWindow.message.find("source line 2497")
                == std::string::npos,
            "source-window read must not leak lines before the requested window");
        require(
            diagnosticWindow.message.find("source line 2503")
                == std::string::npos,
            "source-window read must stop after the requested line count");

        bool unboundedLineCountRejected = false;
        try
        {
            (void)sourceReader.execute(
                rose::tools::ToolRequest{
                    .toolId = "read_text_file",
                    .arguments = {
                        { "path", largeSource.string() },
                        { "start_line", "2400" },
                        { "line_count", "5000" }
                    }
                });
        }
        catch (const std::invalid_argument&)
        {
            unboundedLineCountRejected = true;
        }
        require(
            unboundedLineCountRejected,
            "read_text_file must reject model-selected source windows above the configured line bound");

        bool lineCountWithoutStartRejected = false;
        try
        {
            (void)sourceReader.execute(
                rose::tools::ToolRequest{
                    .toolId = "read_text_file",
                    .arguments = {
                        { "path", largeSource.string() },
                        { "line_count", "20" }
                    }
                });
        }
        catch (const std::invalid_argument&)
        {
            lineCountWithoutStartRejected = true;
        }
        require(
            lineCountWithoutStartRejected,
            "read_text_file line_count must fail closed without start_line");

        // Recursive discovery must remain safe for Rose's 8K local routing
        // context. The default scan still inventories a larger bounded tree, but
        // its model-facing entry list is capped and conventional generated trees
        // are emitted after ordinary/project-owned paths.
        const std::filesystem::path scanBudgetRoot =
            root / "scan-budget";
        const std::filesystem::path scanBudgetBuild =
            scanBudgetRoot / "build";
        const std::filesystem::path scanBudgetSource =
            scanBudgetRoot / "src";

        std::filesystem::create_directories(scanBudgetBuild);
        std::filesystem::create_directories(scanBudgetSource);

        const std::filesystem::path importantSource =
            scanBudgetSource / "important.cpp";
        {
            std::ofstream stream{ importantSource };
            stream << "int important() { return 42; }\n";
        }

        for (std::size_t index{ 0 }; index < 240u; ++index)
        {
            const std::filesystem::path generated =
                scanBudgetBuild
                / (
                    "generated-object-with-a-deliberately-long-name-"
                    + std::to_string(index)
                    + ".obj");
            std::ofstream{ generated } << "x";
        }

        rose::tools::ScanDirectoryTreeTool defaultScanner;
        const rose::tools::ToolResult boundedScan =
            defaultScanner.execute(
                rose::tools::ToolRequest{
                    .toolId = "scan_directory_tree",
                    .arguments = {
                        { "path", scanBudgetRoot.string() },
                        { "max_depth", "2" }
                    }
                });

        require(
            boundedScan.message.size() < 9u * 1024u,
            "default scan observation must stay near the 8 KiB model-context budget");
        require(
            boundedScan.message.find(
                "absolute_path=" + importantSource.lexically_normal().string())
                != std::string::npos,
            "bounded scan output should prioritize useful non-generated grounded paths before build metadata");
        require(
            boundedScan.message.find(
                "[Rose truncated the entry listing to keep the observation bounded.]")
                != std::string::npos,
            "large default scans should advertise observation truncation instead of overflowing model context");


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
