#include "process/ProcessService.h"
#include "shortcuts/ShortcutService.h"
#include "tools/CloseProcessTool.h"
#include "tools/LaunchProgramTool.h"
#include "tools/ListProcessesTool.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error{ message };
    }

    class FakeProcessService final : public rose::process::IProcessService
    {
    public:
        rose::process::LaunchProcessRequest lastLaunch;
        std::uint32_t lastClosedPid{};

        rose::process::LaunchProcessResult launch(const rose::process::LaunchProcessRequest& request) override
        {
            lastLaunch = request;
            return rose::process::LaunchProcessResult{ .processId = 4242, .executablePath = request.executablePath };
        }

        std::vector<rose::process::ProcessInfo> listProcesses(std::size_t maxResults) const override
        {
            std::vector<rose::process::ProcessInfo> values{
                rose::process::ProcessInfo{ .processId = 100, .executableName = "notepad.exe", .imagePath = "C:\\Windows\\System32\\notepad.exe", .hasTopLevelWindow = true },
                rose::process::ProcessInfo{ .processId = 200, .executableName = "worker.exe", .imagePath = "C:\\Tools\\worker.exe", .hasTopLevelWindow = false }
            };
            if (maxResults < values.size()) values.resize(maxResults);
            return values;
        }

        rose::process::CloseProcessResult requestClose(std::uint32_t processId) override
        {
            lastClosedPid = processId;
            return rose::process::CloseProcessResult{
                .processId = processId,
                .executableName = "notepad.exe",
                .imagePath = "C:\\Windows\\System32\\notepad.exe",
                .windowsNotified = 1,
                .exited = false
            };
        }
    };

    class FakeShortcutService final : public rose::shortcuts::IShortcutService
    {
    public:
        std::filesystem::path targetPath;
        std::filesystem::path workingDirectory;

        rose::shortcuts::ShortcutInspection inspect(const std::filesystem::path&) const override
        {
            return rose::shortcuts::ShortcutInspection{
                .kind = "Windows Shell Link (.lnk)",
                .target = targetPath.string(),
                .arguments = "--shortcut",
                .workingDirectory = workingDirectory.string(),
                .description = {},
                .iconLocation = {},
                .url = {},
                .targetExists = true
            };
        }
    };
}

int main()
{
    try
    {
        FakeProcessService processService;
        FakeShortcutService shortcutService;
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "rose-process-tools-test";
        const std::filesystem::path executable = root / "demo.exe";
        const std::filesystem::path shortcut = root / "Demo.lnk";
        shortcutService.targetPath = executable;
        shortcutService.workingDirectory = root;

        rose::tools::LaunchProgramTool launch{ processService, shortcutService };
        require(launch.descriptor().risk == rose::tools::ToolRisk::ExternalEffect,
            "launch_program must be ExternalEffect");
        require(launch.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation,
            "launch_program must require confirmation");

        const auto launchResult = launch.execute(rose::tools::ToolRequest{
            .toolId = "launch_program",
            .arguments = {
                { "path", executable.string() },
                { "arguments", "--safe-mode" },
                { "working_directory", root.string() }
            }
        });
        require(launchResult.success && launchResult.message.find("pid=4242") != std::string::npos,
            "launch_program should surface the launched pid");
        require(processService.lastLaunch.executablePath == executable,
            "launch_program should forward the exact executable path");
        require(processService.lastLaunch.arguments == "--safe-mode",
            "launch_program should preserve explicit arguments");

        (void)launch.execute(rose::tools::ToolRequest{
            .toolId = "launch_program",
            .arguments = {
                { "path", shortcut.string() },
                { "arguments", "--user" }
            }
        });
        require(processService.lastLaunch.executablePath == executable,
            "launch_program should resolve .lnk targets through IShortcutService");
        require(processService.lastLaunch.arguments == "--shortcut --user",
            "launch_program should preserve shortcut arguments before explicit user arguments");

        rose::tools::ListProcessesTool list{ processService };
        require(list.descriptor().risk == rose::tools::ToolRisk::ReadOnly,
            "list_processes must be ReadOnly");
        const auto listResult = list.execute(rose::tools::ToolRequest{
            .toolId = "list_processes",
            .arguments = { { "limit", "1" } }
        });
        require(listResult.message.find("pid=100") != std::string::npos,
            "list_processes should include the process pid");
        require(listResult.message.find("pid=200") == std::string::npos,
            "list_processes should honor the bound");

        rose::tools::CloseProcessTool close{ processService };
        require(close.descriptor().risk == rose::tools::ToolRisk::Destructive,
            "close_process must remain confirmation-forced by destructive risk");
        const auto closeResult = close.execute(rose::tools::ToolRequest{
            .toolId = "close_process",
            .arguments = { { "pid", "100" } }
        });
        require(processService.lastClosedPid == 100,
            "close_process should forward the exact pid");
        require(closeResult.message.find("Rose did not force-terminate") != std::string::npos,
            "close_process should make the graceful-only boundary explicit when still running");

        std::cout << "Rose ProcessTools tests: PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rose ProcessTools tests: FAIL: " << exception.what() << '\n';
        return 1;
    }
}
