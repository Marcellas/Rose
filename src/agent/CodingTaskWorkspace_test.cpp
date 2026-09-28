#include "agent/CodingTaskWorkspace.h"
#include "agent/SourceWindowBinding.h"
#include "files/SourceWindowDigest.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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


    [[nodiscard]]
    rose::tools::SourceWindowEvidence makeEvidence(
        std::string path,
        const std::size_t startLine,
        const std::string& logicalText)
    {
        const std::vector<std::string> lineDigests =
            rose::files::sourceLineSha256s(
                logicalText);

        return rose::tools::SourceWindowEvidence{
            .path = std::move(path),
            .startLine = startLine,
            .lineCount = lineDigests.size(),
            .lineSha256s = lineDigests,
            .sha256 =
                rose::files::sourceWindowSha256FromLineDigests(
                    lineDigests)
        };
    }
}


int main()
{
    try
    {
        rose::agent::CodingTaskWorkspaceState workspace;

        const auto headerWindow =
            makeEvidence(
                "C:/Rose/src/example.h",
                10,
                "class Example\n{\npublic:\n    void run();\n};");

        const auto sourceWindow =
            makeEvidence(
                "C:/Rose/src/example.cpp",
                40,
                "void Example::run()\n{\n    old_call();\n}");

        rose::agent::observeCodingSourceWindow(
            workspace,
            headerWindow);

        rose::agent::observeCodingSourceWindow(
            workspace,
            sourceWindow);

        require(
            workspace.sourceWindows.size() == 2,
            "two source reads from different files should remain available in one bounded coding workspace");

        const rose::tools::ToolRequest headerPatch{
            .toolId = "edit_text_file",
            .arguments = {
                { "path", "C:/Rose/src/example.h" },
                { "operation", "replace_line_range" },
                { "start_line", "13" },
                { "line_count", "1" },
                { "expected_text", "    void run();" },
                { "replacement_text", "    void run() const;" }
            }
        };

        const auto retainedHeader =
            rose::agent::sourceWindowForRequest(
                workspace,
                headerPatch);

        require(
            retainedHeader.has_value()
                && retainedHeader->path == "C:/Rose/src/example.h",
            "a later edit should recover provenance for an earlier file even after another file was read");

        const rose::tools::ToolRequest boundHeader =
            rose::agent::bindSourceWindowEvidence(
                headerPatch,
                retainedHeader);

        require(
            !boundHeader.arguments.contains("expected_text")
                && boundHeader.arguments.contains("expected_digest"),
            "retained multi-file provenance should bind the earlier file patch to a Rose-owned digest");

        const rose::tools::ToolRequest sourcePatch{
            .toolId = "edit_text_file",
            .arguments = {
                { "path", "C:/Rose/src/example.cpp" },
                { "operation", "replace_line_range" },
                { "start_line", "42" },
                { "line_count", "1" },
                { "expected_text", "    old_call();" },
                { "replacement_text", "    new_call();" }
            }
        };

        require(
            rose::agent::sourceWindowForRequest(
                workspace,
                sourcePatch).has_value(),
            "the second file should independently retain its own source-window provenance");

        rose::agent::observeCodingMutation(
            workspace,
            boundHeader,
            rose::tools::ToolResult{
                .success = true,
                .message = "header edited",
                .trustedMetadata = {},
                .sourceWindowEvidence = std::nullopt,
                .responseMode =
                    rose::tools::ToolResponseMode::RequiresModelSynthesis,
                .artifacts = {}
            });

        require(
            !rose::agent::sourceWindowForRequest(
                workspace,
                headerPatch).has_value(),
            "a successful edit must invalidate every retained pre-edit window for that exact path");

        require(
            rose::agent::sourceWindowForRequest(
                workspace,
                sourcePatch).has_value(),
            "editing one file must not discard provenance for a different observed file");

        require(
            workspace.editedPaths.size() == 1
                && workspace.editedPaths.front() == "C:/Rose/src/example.h",
            "the workspace should retain a compact changed-file summary without storing replacement contents");

        const std::string metadata =
            rose::agent::formatCodingTaskWorkspaceMetadata(
                workspace);

        require(
            metadata.find("metadata_kind=coding_task_workspace")
                    != std::string::npos
                && metadata.find("C:/Rose/src/example.cpp")
                    != std::string::npos
                && metadata.find("C:/Rose/src/example.h")
                    != std::string::npos
                && metadata.find(sourceWindow.sha256)
                    == std::string::npos
                && metadata.find("new_call")
                    == std::string::npos,
            "control metadata should expose bounded multi-file scope without leaking source hashes or mutation payloads");

        // A narrower read should be preferred over an older broad read when both
        // cover the requested patch range.
        const auto broadWindow =
            makeEvidence(
                "C:/Rose/src/narrow.cpp",
                100,
                "a\nb\nc\nd\ne");

        const auto narrowWindow =
            makeEvidence(
                "C:/Rose/src/narrow.cpp",
                102,
                "c\nd");

        rose::agent::observeCodingSourceWindow(
            workspace,
            broadWindow);

        rose::agent::observeCodingSourceWindow(
            workspace,
            narrowWindow);

        const rose::tools::ToolRequest narrowPatch{
            .toolId = "edit_text_file",
            .arguments = {
                { "path", "C:/Rose/src/narrow.cpp" },
                { "operation", "replace_line_range" },
                { "start_line", "102" },
                { "line_count", "2" },
                { "expected_text", "c\nd" },
                { "replacement_text", "x\ny" }
            }
        };

        const auto selectedNarrow =
            rose::agent::sourceWindowForRequest(
                workspace,
                narrowPatch);

        require(
            selectedNarrow.has_value()
                && selectedNarrow->startLine == 102
                && selectedNarrow->lineCount == 2,
            "the smallest retained source window that covers a patch should be selected");

        // Storage remains bounded. Oldest observations are evicted first.
        rose::agent::CodingTaskWorkspaceState bounded;

        for (
            std::size_t index{ 0 };
            index < rose::agent::maximumRetainedSourceWindows + 1u;
            ++index)
        {
            rose::agent::observeCodingSourceWindow(
                bounded,
                makeEvidence(
                    "C:/Rose/src/file"
                        + std::to_string(index)
                        + ".cpp",
                    1,
                    "line"));
        }

        require(
            bounded.sourceWindows.size()
                == rose::agent::maximumRetainedSourceWindows
                && bounded.sourceWindows.front().path
                    == "C:/Rose/src/file1.cpp",
            "coding workspace source evidence must stay bounded and evict the oldest window first");

        rose::agent::observeCodingMutation(
            bounded,
            rose::tools::ToolRequest{
                .toolId = "move_path",
                .arguments = {
                    { "source_path", "C:/Rose/src" },
                    { "destination_path", "C:/Rose/src2" }
                }
            },
            rose::tools::ToolResult{
                .success = true,
                .message = "moved",
                .trustedMetadata = {},
                .sourceWindowEvidence = std::nullopt,
                .responseMode =
                    rose::tools::ToolResponseMode::RequiresModelSynthesis,
                .artifacts = {}
            });

        require(
            bounded.sourceWindows.empty()
                && bounded.editedPaths.empty(),
            "path-shaping mutations must clear retained source provenance rather than guessing which absolute paths remain valid");

        std::cout
            << "Rose CodingTaskWorkspace tests: PASS\n";

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "CodingTaskWorkspace test failed: "
            << exception.what()
            << '\n';

        return 1;
    }
}
