#include "database/DatabaseService.h"
#include "permissions/PermissionSystem.h"
#include "shortcuts/ShortcutService.h"
#include "tools/InspectDatabaseRegisteredTool.h"
#include "tools/InspectShortcutRegisteredTool.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error{ message };
    }

    class FakeDatabaseService final : public rose::database::IDatabaseService
    {
    public:
        [[nodiscard]] bool availableFor(const std::filesystem::path&) const noexcept override { return true; }
        [[nodiscard]] std::string availabilityMessage(const std::filesystem::path&) const override { return "fake available"; }
        [[nodiscard]] rose::database::DatabaseInspection inspect(
            const std::filesystem::path&,
            std::size_t,
            std::size_t) const override
        {
            rose::database::DatabaseObject table;
            table.type = "table";
            table.name = "people";
            table.definition = "CREATE TABLE people(id INTEGER, name TEXT)";
            table.sampleRows = {
                rose::database::DatabaseRowSample{ {
                    { "id", "1" }, { "name", "Ada" }
                } }
            };
            return rose::database::DatabaseInspection{
                .family = "SQLite",
                .backend = "fake-readonly",
                .objects = { std::move(table) },
                .truncated = false
            };
        }
    };

    class FakeShortcutService final : public rose::shortcuts::IShortcutService
    {
    public:
        [[nodiscard]] rose::shortcuts::ShortcutInspection inspect(const std::filesystem::path&) const override
        {
            return rose::shortcuts::ShortcutInspection{
                .kind = "Windows Shell Link (.lnk)",
                .target = "C:\\Program Files\\Editor\\editor.exe",
                .arguments = "--safe",
                .workingDirectory = "C:\\Program Files\\Editor",
                .description = "Editor",
                .iconLocation = "editor.exe,0",
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
        rose::permissions::PermissionSystem permissions;
        FakeDatabaseService database;
        rose::tools::InspectDatabaseRegisteredTool databaseTool{ permissions, database };
        const auto dbPath = std::filesystem::temp_directory_path() / "rose-database-test.sqlite";
        require(dbPath.is_absolute(), "database fixture must be absolute");
        const auto databaseResult = databaseTool.execute(rose::tools::ToolRequest{
            .toolId = "inspect_database",
            .arguments = { { "path", dbPath.string() } }
        });
        require(databaseResult.success, "database inspection should succeed");
        require(databaseResult.message.find("people") != std::string::npos,
                "database observation should include table name");
        require(databaseResult.message.find("Ada") != std::string::npos,
                "database observation should include bounded row sample");
        require(databaseResult.message.find("does not imply the entire database") != std::string::npos,
                "database observation should disclose bounded sampling");

        FakeShortcutService shortcut;
        rose::tools::InspectShortcutRegisteredTool shortcutTool{ permissions, shortcut };
        const auto lnkPath = std::filesystem::temp_directory_path() / "Editor.lnk";
        const auto shortcutResult = shortcutTool.execute(rose::tools::ToolRequest{
            .toolId = "inspect_shortcut",
            .arguments = { { "path", lnkPath.string() } }
        });
        require(shortcutResult.success, "shortcut inspection should succeed");
        require(shortcutResult.message.find("editor.exe") != std::string::npos,
                "shortcut observation should include target");
        require(shortcutResult.message.find("did not launch") != std::string::npos,
                "shortcut observation should state non-launch behavior");

        // .url parsing is portable and should not require Windows COM.
        const auto urlPath = std::filesystem::temp_directory_path() / "rose-shortcut-test.url";
        {
            std::ofstream file{ urlPath, std::ios::binary };
            file << "[InternetShortcut]\nURL=https://example.com/docs\nIconFile=C:\\icons\\docs.ico\n";
        }
        rose::shortcuts::LocalShortcutService localShortcut;
        const auto url = localShortcut.inspect(urlPath);
        require(url.url == "https://example.com/docs", ".url reader should parse URL metadata");
        std::error_code error;
        std::filesystem::remove(urlPath, error);

        std::cout << "Rose DatabaseShortcutTools tests: PASS\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rose DatabaseShortcutTools tests: FAIL: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
