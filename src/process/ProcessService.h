#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rose::process
{
    struct ProcessInfo
    {
        std::uint32_t processId{ 0 };
        std::string executableName;
        std::string imagePath;
        bool hasTopLevelWindow{ false };
    };

    struct LaunchProcessRequest
    {
        std::filesystem::path executablePath;
        std::string arguments;
        std::optional<std::filesystem::path> workingDirectory;
    };

    struct LaunchProcessResult
    {
        std::uint32_t processId{ 0 };
        std::filesystem::path executablePath;
    };

    struct CloseProcessResult
    {
        std::uint32_t processId{ 0 };
        std::string executableName;
        std::string imagePath;
        std::size_t windowsNotified{ 0 };
        bool exited{ false };
    };

    class IProcessService
    {
    public:
        virtual ~IProcessService() = default;

        [[nodiscard]] virtual LaunchProcessResult launch(
            const LaunchProcessRequest& request) = 0;

        [[nodiscard]] virtual std::vector<ProcessInfo> listProcesses(
            std::size_t maxResults) const = 0;

        [[nodiscard]] virtual CloseProcessResult requestClose(
            std::uint32_t processId) = 0;
    };

    class LocalProcessService final : public IProcessService
    {
    public:
        [[nodiscard]] LaunchProcessResult launch(
            const LaunchProcessRequest& request) override;

        [[nodiscard]] std::vector<ProcessInfo> listProcesses(
            std::size_t maxResults) const override;

        [[nodiscard]] CloseProcessResult requestClose(
            std::uint32_t processId) override;
    };
}
