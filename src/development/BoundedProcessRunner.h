#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rose::development
{
    struct BoundedProcessRequest
    {
        std::filesystem::path executable;
        std::vector<std::wstring> arguments;
        std::filesystem::path workingDirectory;
        std::chrono::milliseconds timeout{ std::chrono::minutes{ 10 } };
        std::size_t maximumCapturedBytes{ 32U * 1024U };
        std::size_t preservedHeadBytes{ 4U * 1024U };
        std::string operationName{ "child process" };
    };

    struct BoundedProcessResult
    {
        std::uint32_t exitCode{ 0 };
        bool timedOut{ false };
        bool outputTruncated{ false };
        std::string output;
    };

    // Execute one explicitly selected child process without a command shell.
    //
    // Windows implementation details are intentionally isolated here so future
    // development tools can share the same bounded process semantics instead of
    // duplicating CreateProcessW/pipe/Job Object code in every service.
    //
    // Security properties:
    // - lpApplicationName is set to the exact executable path supplied by caller.
    // - no cmd.exe / PowerShell / shell expansion is involved.
    // - stdin is NUL so the child cannot wait for interactive input.
    // - stdout/stderr are captured into a bounded head+tail buffer.
    // - the process tree is placed in a kill-on-close Job Object.
    // - timeout termination reaches only the child job Rose created.
    [[nodiscard]]
    BoundedProcessResult runBoundedProcess(
        const BoundedProcessRequest& request);

    // Resolve one exact executable name through Windows SearchPathW. This remains
    // an internal service helper; model/tool requests never supply this value.
    [[nodiscard]]
    std::filesystem::path findExecutableOnPath(
        std::wstring_view executableName);
}
