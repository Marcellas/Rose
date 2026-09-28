#include "files/TextFileMutationService.h"
#include "files/SourceWindowDigest.h"
#include "tools/EditTextFileTool.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
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

    template<typename Function>
    void requireThrows(
        Function&& function,
        const std::string& message)
    {
        bool threw{ false };
        try
        {
            function();
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        require(threw, message);
    }

    void writeBytes(
        const std::filesystem::path& path,
        const std::string_view bytes)
    {
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        if (!output)
        {
            throw std::runtime_error{ "Could not create text-mutation test fixture." };
        }

        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output)
        {
            throw std::runtime_error{ "Could not write text-mutation test fixture." };
        }
    }

    [[nodiscard]]
    std::string readBytes(
        const std::filesystem::path& path)
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "Could not read text-mutation test fixture." };
        }

        return std::string{
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{}
        };
    }

    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            const auto stamp =
                std::chrono::steady_clock::now().time_since_epoch().count();

            path_ = std::filesystem::temp_directory_path()
                / ("rose-text-mutation-test-" + std::to_string(stamp));

            std::filesystem::create_directories(path_);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]]
        const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    class FakeTextMutationService final
        : public rose::files::ITextFileMutationService
    {
    public:
        rose::files::EditTextFileRequest lastRequest;
        std::size_t calls{};

        rose::files::TextFileMutationResult edit(
            const rose::files::EditTextFileRequest& request) override
        {
            lastRequest = request;
            ++calls;

            return rose::files::TextFileMutationResult{
                .operation = "fake_edit",
                .detail = request.path.string(),
                .affectedCount = 1
            };
        }
    };
}

int main()
{
    try
    {
        TemporaryDirectory temporary;
        rose::files::LocalTextFileMutationService service;

        const std::filesystem::path source = temporary.path() / "sample.cpp";
        writeBytes(source, "alpha\r\nbeta\r\ngamma\r\n");

        const auto replaced = service.edit(
            rose::files::EditTextFileRequest{
                .path = source,
                .kind = rose::files::TextFileMutationKind::ReplaceExactText,
                .findText = "beta",
                .replacementText = "BETA"
            });

        require(replaced.operation == "replace_text" && replaced.affectedCount == 1,
            "replace_text should report one affected unique match");
        require(readBytes(source) == "alpha\r\nBETA\r\ngamma\r\n",
            "replace_text must preserve bytes outside the exact replacement, including CRLF");

        const auto appended = service.edit(
            rose::files::EditTextFileRequest{
                .path = source,
                .kind = rose::files::TextFileMutationKind::AppendText,
                .text = "// tail\r\n"
            });

        require(appended.operation == "append_text",
            "append_text should report its exact operation");
        require(readBytes(source) == "alpha\r\nBETA\r\ngamma\r\n// tail\r\n",
            "append_text must append exactly without inventing a separator");

        (void)service.edit(
            rose::files::EditTextFileRequest{
                .path = source,
                .kind = rose::files::TextFileMutationKind::RemoveText,
                .findText = "BETA\r\n"
            });

        require(readBytes(source) == "alpha\r\ngamma\r\n// tail\r\n",
            "remove_text should remove exactly one unique byte sequence");

        const std::filesystem::path linePatch = temporary.path() / "line-patch.cpp";
        const std::string linePatchBom{ "\xEF\xBB\xBF", 3 };
        writeBytes(
            linePatch,
            linePatchBom + "one\r\ntwo\r\nthree\r\nfour\r\n");

        const auto patchedLines = service.edit(
            rose::files::EditTextFileRequest{
                .path = linePatch,
                .kind = rose::files::TextFileMutationKind::ReplaceLineRange,
                .replacementText = "TWO\nTHREE!",
                .startLine = 2,
                .lineCount = 2,
                .expectedText = "two\nthree"
            });

        require(
            patchedLines.operation == "replace_line_range"
                && patchedLines.affectedCount == 2,
            "replace_line_range should report the exact number of selected source lines");
        require(
            readBytes(linePatch)
                == linePatchBom + "one\r\nTWO\r\nTHREE!\r\nfour\r\n",
            "replace_line_range must preserve the UTF-8 BOM, CRLF style, and bytes outside the selected lines");

        const std::string linePatchBeforeMismatch = readBytes(linePatch);
        requireThrows(
            [&]
            {
                (void)service.edit(
                    rose::files::EditTextFileRequest{
                        .path = linePatch,
                        .kind = rose::files::TextFileMutationKind::ReplaceLineRange,
                        .replacementText = "replacement",
                        .startLine = 2,
                        .lineCount = 2,
                        .expectedText = "TWO\nstale preimage"
                    });
            },
            "replace_line_range must fail closed when the observed preimage is stale");
        require(
            readBytes(linePatch) == linePatchBeforeMismatch,
            "preimage mismatch must leave the original line-patch target untouched");

        const std::vector<std::string> observedLineDigests =
            rose::files::sourceLineSha256s("TWO\nTHREE!");
        require(
            observedLineDigests.size() == 2
                && observedLineDigests[0]
                    == "a1a8a8cbbed4eb53ae62ee4fb0787504087232c29aa4d817757d06b68d0501ca"
                && observedLineDigests[1]
                    == "15f5faacba378f58a49cad56954c709c7436306b0a16434da633979335298824",
            "per-line SHA-256 should remain stable across platforms");

        const std::string observedDigest =
            rose::files::sourceWindowSha256FromLineDigests(
                observedLineDigests);

        const auto digestPatchedLines = service.edit(
            rose::files::EditTextFileRequest{
                .path = linePatch,
                .kind = rose::files::TextFileMutationKind::ReplaceLineRange,
                .replacementText = "digest-two\ndigest-three",
                .startLine = 2,
                .lineCount = 2,
                .expectedDigest = observedDigest
            });
        require(
            digestPatchedLines.affectedCount == 2
                && readBytes(linePatch)
                    == linePatchBom + "one\r\ndigest-two\r\ndigest-three\r\nfour\r\n",
            "replace_line_range should accept Rose-owned SHA-256 source-window provenance without model-echoed preimage text");

        const std::string digestPatchBeforeStale = readBytes(linePatch);
        requireThrows(
            [&]
            {
                (void)service.edit(
                    rose::files::EditTextFileRequest{
                        .path = linePatch,
                        .kind = rose::files::TextFileMutationKind::ReplaceLineRange,
                        .replacementText = "should-not-apply",
                        .startLine = 2,
                        .lineCount = 2,
                        .expectedDigest = observedDigest
                    });
            },
            "replace_line_range must fail closed when source-window digest provenance is stale");
        require(
            readBytes(linePatch) == digestPatchBeforeStale,
            "digest preimage mismatch must leave the original target untouched");

        const std::filesystem::path emptyLinePatch =
            temporary.path() / "empty-line.cpp";
        writeBytes(emptyLinePatch, "first\r\n\r\nthird\r\n");
        (void)service.edit(
            rose::files::EditTextFileRequest{
                .path = emptyLinePatch,
                .kind = rose::files::TextFileMutationKind::ReplaceLineRange,
                .replacementText = "inserted-a\ninserted-b",
                .startLine = 2,
                .lineCount = 1,
                .expectedText = ""
            });
        require(
            readBytes(emptyLinePatch)
                == "first\r\ninserted-a\r\ninserted-b\r\nthird\r\n",
            "replace_line_range should support an empty one-line preimage and bounded line expansion");

        requireThrows(
            [&]
            {
                (void)service.edit(
                    rose::files::EditTextFileRequest{
                        .path = linePatch,
                        .kind = rose::files::TextFileMutationKind::ReplaceLineRange,
                        .replacementText = "x",
                        .startLine = 1,
                        .lineCount = 201,
                        .expectedText = "x"
                    });
            },
            "replace_line_range must enforce the 200-line patch bound");

        const std::filesystem::path ambiguous = temporary.path() / "ambiguous.txt";
        writeBytes(ambiguous, "aaa");
        const std::string ambiguousBefore = readBytes(ambiguous);
        requireThrows(
            [&]
            {
                (void)service.edit(
                    rose::files::EditTextFileRequest{
                        .path = ambiguous,
                        .kind = rose::files::TextFileMutationKind::RemoveText,
                        .findText = "aa"
                    });
            },
            "overlapping duplicate find_text must be treated as ambiguous");
        require(readBytes(ambiguous) == ambiguousBefore,
            "ambiguous mutation must leave the original untouched");

        const std::filesystem::path missing = temporary.path() / "missing.json";
        writeBytes(missing, "{\"value\":1}\n");
        const std::string missingBefore = readBytes(missing);
        requireThrows(
            [&]
            {
                (void)service.edit(
                    rose::files::EditTextFileRequest{
                        .path = missing,
                        .kind = rose::files::TextFileMutationKind::ReplaceExactText,
                        .findText = "not-present",
                        .replacementText = "replacement"
                    });
            },
            "missing find_text must fail closed");
        require(readBytes(missing) == missingBefore,
            "missing-match mutation must leave the original untouched");

        const std::filesystem::path invalidUtf8 = temporary.path() / "invalid.txt";
        writeBytes(invalidUtf8, std::string{ "\xC3\x28", 2 });
        requireThrows(
            [&]
            {
                (void)service.edit(
                    rose::files::EditTextFileRequest{
                        .path = invalidUtf8,
                        .kind = rose::files::TextFileMutationKind::AppendText,
                        .text = "x"
                    });
            },
            "invalid UTF-8 source must be rejected");

        const std::filesystem::path binary = temporary.path() / "binary.txt";
        const std::string binaryBytes{ "a\0b", 3 };
        writeBytes(binary, binaryBytes);
        requireThrows(
            [&]
            {
                (void)service.edit(
                    rose::files::EditTextFileRequest{
                        .path = binary,
                        .kind = rose::files::TextFileMutationKind::AppendText,
                        .text = "x"
                    });
            },
            "NUL-containing source must be rejected as binary");

        const std::filesystem::path executable = temporary.path() / "program.exe";
        writeBytes(executable, "plain text bytes");
        requireThrows(
            [&]
            {
                (void)service.edit(
                    rose::files::EditTextFileRequest{
                        .path = executable,
                        .kind = rose::files::TextFileMutationKind::AppendText,
                        .text = "x"
                    });
            },
            "recognized executable formats must not be editable through generic text mutation");

        const std::filesystem::path commandScript = temporary.path() / "script.cmd";
        writeBytes(commandScript, "echo hello\r\n");
        requireThrows(
            [&]
            {
                (void)service.edit(
                    rose::files::EditTextFileRequest{
                        .path = commandScript,
                        .kind = rose::files::TextFileMutationKind::AppendText,
                        .text = "echo later\r\n"
                    });
            },
            "batch command scripts should remain outside generic text mutation authority");

        const std::filesystem::path cmake = temporary.path() / "CMakeLists.txt";
        writeBytes(cmake, "project(Rose)\n");
        (void)service.edit(
            rose::files::EditTextFileRequest{
                .path = cmake,
                .kind = rose::files::TextFileMutationKind::AppendText,
                .text = "add_subdirectory(src)\n"
            });
        require(readBytes(cmake) == "project(Rose)\nadd_subdirectory(src)\n",
            "CMakeLists.txt should be a recognized editable source file");

        const std::filesystem::path bom = temporary.path() / "bom.md";
        const std::string bomSource = std::string{ "\xEF\xBB\xBF", 3 } + "hello\n";
        writeBytes(bom, bomSource);
        (void)service.edit(
            rose::files::EditTextFileRequest{
                .path = bom,
                .kind = rose::files::TextFileMutationKind::AppendText,
                .text = "world\n"
            });
        require(readBytes(bom) == bomSource + "world\n",
            "UTF-8 BOM and existing bytes must be preserved exactly");

        FakeTextMutationService fake;
        rose::tools::EditTextFileTool tool{ fake };

        require(tool.descriptor().risk == rose::tools::ToolRisk::LocalWrite,
            "edit_text_file must be LocalWrite");
        require(tool.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation,
            "edit_text_file must require explicit confirmation");

        (void)tool.execute(
            rose::tools::ToolRequest{
                .toolId = "edit_text_file",
                .arguments = {
                    { "path", source.string() },
                    { "operation", "replace_text" },
                    { "find_text", "old\\nline" },
                    { "replacement_text", "new\\\\nline" }
                }
            });

        require(fake.calls == 1,
            "edit_text_file adapter should call its service exactly once");
        require(fake.lastRequest.kind == rose::files::TextFileMutationKind::ReplaceExactText,
            "replace_text should map to TextFileMutationKind::ReplaceExactText");
        require(fake.lastRequest.findText == "old\nline",
            "edit_text_file should decode newline escapes for exact matching");
        require(fake.lastRequest.replacementText == "new\\nline",
            "double backslash should preserve a literal source-code \\n sequence");

        (void)tool.execute(
            rose::tools::ToolRequest{
                .toolId = "edit_text_file",
                .arguments = {
                    { "path", source.string() },
                    { "operation", "replace_line_range" },
                    { "start_line", "41" },
                    { "line_count", "2" },
                    { "expected_text", "\\s\\sold one\nold two" },
                    { "replacement_text", "\\s\\snew one\nnew two\\s" }
                }
            });

        require(fake.calls == 2,
            "replace_line_range adapter should call its service exactly once");
        require(
            fake.lastRequest.kind == rose::files::TextFileMutationKind::ReplaceLineRange,
            "replace_line_range should map to TextFileMutationKind::ReplaceLineRange");
        require(
            fake.lastRequest.startLine == 41 && fake.lastRequest.lineCount == 2,
            "replace_line_range should preserve one-based line coordinates");
        require(
            fake.lastRequest.expectedText == "  old one\nold two"
                && fake.lastRequest.replacementText == "  new one\nnew two ",
            "replace_line_range should decode logical newlines and explicit edge-space escapes");

        const std::string adapterDigest =
            rose::files::sourceWindowSha256("old one");
        (void)tool.execute(
            rose::tools::ToolRequest{
                .toolId = "edit_text_file",
                .arguments = {
                    { "path", source.string() },
                    { "operation", "replace_line_range" },
                    { "start_line", "7" },
                    { "line_count", "1" },
                    { "expected_digest", adapterDigest },
                    { "replacement_text", "new one" }
                }
            });
        require(
            fake.calls == 3
                && fake.lastRequest.expectedText.empty()
                && fake.lastRequest.expectedDigest == adapterDigest,
            "edit_text_file should carry source-window digest provenance to the mutation service without source-text decoding");

        std::cout << "Rose TextMutationTools tests: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Rose TextMutationTools tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
