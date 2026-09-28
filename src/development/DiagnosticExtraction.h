#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rose::development
{
    // One compiler/CMake/test diagnostic that Rose has grounded to a regular
    // file inside the configured project tree. Paths outside sourceDirectory
    // are deliberately discarded so project-controlled build output cannot
    // manufacture read authority for unrelated local files.
    struct SourceDiagnostic
    {
        std::filesystem::path path;
        std::size_t line{ 0 };
        std::size_t column{ 0 };
        std::string severity;
        std::string code;
        std::string message;
    };

    // Extracts a small bounded set of source diagnostics from common MSVC,
    // GCC/Clang, CMake, and test-failure output shapes. The parser is evidence
    // extraction only; it never executes anything and never reads candidate
    // files outside the configured project root.
    [[nodiscard]]
    std::vector<SourceDiagnostic> extractSourceDiagnostics(
        std::string_view output,
        const std::filesystem::path& sourceDirectory,
        std::size_t maximumDiagnostics = 12);

    [[nodiscard]]
    std::size_t suggestedDiagnosticStartLine(
        std::size_t diagnosticLine) noexcept;

    // Builds a small Rose-owned metadata block from already-grounded diagnostics.
    // Only the primary diagnostic is included because deterministic routing needs
    // one exact next source window, not a second copy of the complete build log.
    [[nodiscard]]
    std::string buildTrustedDiagnosticMetadata(
        std::string_view producerToolId,
        bool operationSucceeded,
        std::span<const SourceDiagnostic> diagnostics);

    [[nodiscard]]
    constexpr std::size_t suggestedDiagnosticLineCount() noexcept
    {
        return 80;
    }
}
