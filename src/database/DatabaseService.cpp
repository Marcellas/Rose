#include "database/DatabaseService.h"

#include "integrations/SimpleJson.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace rose::database
{
    namespace
    {
        [[maybe_unused]] [[nodiscard]] std::string lowerExtension(const std::filesystem::path& path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return extension;
        }

        [[maybe_unused]] [[nodiscard]] bool accessExtension(const std::filesystem::path& path)
        {
            const auto extension = lowerExtension(path);
            return extension == ".mdb" || extension == ".accdb";
        }

        [[maybe_unused]] [[nodiscard]] bool sqliteExtension(const std::filesystem::path& path)
        {
            const auto extension = lowerExtension(path);
            return extension == ".sqlite" || extension == ".sqlite3" || extension == ".db"
                || extension == ".db3" || extension == ".sqlitedb";
        }

        [[maybe_unused]] [[nodiscard]] bool hasSqliteHeader(const std::filesystem::path& path)
        {
            std::ifstream file{ path, std::ios::binary };
            std::array<char, 16> header{};
            if (!file.read(header.data(), static_cast<std::streamsize>(header.size()))) return false;
            static constexpr std::array<char, 16> magic{
                'S','Q','L','i','t','e',' ','f','o','r','m','a','t',' ','3','\0'
            };
            return header == magic;
        }

        [[maybe_unused]] [[nodiscard]] std::string quoteSqliteIdentifier(const std::string& value)
        {
            std::string result{ "\"" };
            for (const char character : value)
            {
                if (character == '"') result += "\"\"";
                else result.push_back(character);
            }
            result.push_back('"');
            return result;
        }

        [[maybe_unused]] [[nodiscard]] std::string primitiveText(const integrations::json::Value& value)
        {
            if (const auto* text = value.string()) return *text;
            if (const auto number = value.number())
            {
                std::ostringstream stream;
                stream << std::setprecision(15) << *number;
                return stream.str();
            }
            if (const auto boolean = value.boolean()) return *boolean ? "true" : "false";
            return "NULL";
        }

        [[maybe_unused]] [[nodiscard]] std::vector<DatabaseRowSample> parseRows(
            const integrations::json::Value& value,
            const std::size_t maximumRows)
        {
            std::vector<DatabaseRowSample> rows;
            const auto* array = value.array();
            if (!array) return rows;
            for (const auto& item : *array)
            {
                if (rows.size() >= maximumRows) break;
                const auto* object = item.object();
                if (!object) continue;
                DatabaseRowSample row;
                for (const auto& [name, cell] : *object)
                    row.columns.push_back(DatabaseColumnSample{ name, primitiveText(cell) });
                rows.push_back(std::move(row));
            }
            return rows;
        }

#ifdef _WIN32
        [[nodiscard]] std::wstring quoteWindowsArgument(const std::wstring& argument)
        {
            if (argument.empty()) return L"\"\"";
            if (argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
            std::wstring result{ L'\"' };
            std::size_t slashes{};
            for (const wchar_t character : argument)
            {
                if (character == L'\\') { ++slashes; continue; }
                if (character == L'\"')
                {
                    result.append(slashes * 2 + 1, L'\\');
                    result.push_back(L'\"');
                    slashes = 0;
                    continue;
                }
                result.append(slashes, L'\\');
                slashes = 0;
                result.push_back(character);
            }
            result.append(slashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }

        class TemporaryWorkspace final
        {
        public:
            TemporaryWorkspace()
            {
                static std::atomic<std::uint64_t> sequence{ 0 };
                const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
                root_ = std::filesystem::temp_directory_path()
                    / ("Rose-database-" + std::to_string(now) + "-" + std::to_string(sequence.fetch_add(1)));
                std::error_code error;
                if (!std::filesystem::create_directories(root_, error) || error)
                    throw std::runtime_error{ "Could not create Rose's temporary database workspace." };
            }
            ~TemporaryWorkspace()
            {
                std::error_code error;
                std::filesystem::remove_all(root_, error);
            }
            [[nodiscard]] const std::filesystem::path& path() const noexcept { return root_; }
        private:
            std::filesystem::path root_;
        };

        [[nodiscard]] std::filesystem::path resolveExecutable(
            const std::wstring_view fileName,
            const std::vector<std::filesystem::path>& localCandidates)
        {
            for (const auto& candidate : localCandidates)
            {
                std::error_code error;
                if (std::filesystem::is_regular_file(candidate, error) && !error)
                    return std::filesystem::absolute(candidate, error);
            }

            std::array<wchar_t, 32768> buffer{};
            const DWORD length = SearchPathW(
                nullptr, std::wstring{ fileName }.c_str(), nullptr,
                static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
            if (length > 0 && length < buffer.size())
                return std::filesystem::path{ std::wstring{ buffer.data(), length } };
            return {};
        }

        [[nodiscard]] std::string runProcessCapture(
            const std::filesystem::path& executable,
            const std::vector<std::wstring>& arguments,
            const DWORD timeoutMilliseconds = 60000)
        {
            TemporaryWorkspace workspace;
            const auto outputPath = workspace.path() / "stdout.txt";
            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;
            HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
            if (output == INVALID_HANDLE_VALUE)
                throw std::runtime_error{ "Could not create database helper output file." };

            std::wstring command = quoteWindowsArgument(executable.wstring());
            for (const auto& argument : arguments)
            {
                command.push_back(L' ');
                command += quoteWindowsArgument(argument);
            }

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = output;
            startup.hStdError = output;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            PROCESS_INFORMATION process{};
            std::vector<wchar_t> mutableCommand(command.begin(), command.end());
            mutableCommand.push_back(L'\0');

            const BOOL created = CreateProcessW(
                nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
            CloseHandle(output);
            if (!created) throw std::runtime_error{ "Could not start local database helper process." };

            const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMilliseconds);
            if (wait == WAIT_TIMEOUT)
            {
                TerminateProcess(process.hProcess, 1);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                throw std::runtime_error{ "Local database inspection timed out." };
            }
            DWORD exitCode{};
            GetExitCodeProcess(process.hProcess, &exitCode);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);

            std::ifstream file{ outputPath, std::ios::binary };
            std::string text{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
            if (exitCode != 0)
                throw std::runtime_error{ "Local database helper failed: " + text };
            return text;
        }

        struct sqlite3;
        struct sqlite3_stmt;

        [[nodiscard]] std::string utf8FromWide(const std::wstring_view text)
        {
            if (text.empty()) return {};
            const int required = WideCharToMultiByte(
                CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                nullptr, 0, nullptr, nullptr);
            if (required <= 0) throw std::runtime_error{ "Could not encode database path as UTF-8." };
            std::string result(static_cast<std::size_t>(required), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                result.data(), required, nullptr, nullptr);
            return result;
        }

        class SqliteApi final
        {
        public:
            using OpenV2 = int(__cdecl*)(const char*, sqlite3**, int, const char*);
            using Close = int(__cdecl*)(sqlite3*);
            using PrepareV2 = int(__cdecl*)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
            using Step = int(__cdecl*)(sqlite3_stmt*);
            using Finalize = int(__cdecl*)(sqlite3_stmt*);
            using ColumnCount = int(__cdecl*)(sqlite3_stmt*);
            using ColumnName = const char*(__cdecl*)(sqlite3_stmt*, int);
            using ColumnText = const unsigned char*(__cdecl*)(sqlite3_stmt*, int);
            using Errmsg = const char*(__cdecl*)(sqlite3*);

            SqliteApi()
            {
                const std::array<std::filesystem::path, 2> localCandidates{
                    std::filesystem::current_path() / "tools/sqlite/winsqlite3.dll",
                    std::filesystem::current_path() / "tools/sqlite/sqlite3.dll"
                };
                for (const auto& candidate : localCandidates)
                {
                    std::error_code error;
                    if (std::filesystem::is_regular_file(candidate, error) && !error)
                    {
                        module_ = LoadLibraryW(candidate.c_str());
                        if (module_) break;
                    }
                }
                if (!module_) module_ = LoadLibraryW(L"winsqlite3.dll");
                if (!module_) module_ = LoadLibraryW(L"sqlite3.dll");
                if (!module_) return;

                openV2 = load<OpenV2>("sqlite3_open_v2");
                close = load<Close>("sqlite3_close");
                prepareV2 = load<PrepareV2>("sqlite3_prepare_v2");
                step = load<Step>("sqlite3_step");
                finalize = load<Finalize>("sqlite3_finalize");
                columnCount = load<ColumnCount>("sqlite3_column_count");
                columnName = load<ColumnName>("sqlite3_column_name");
                columnText = load<ColumnText>("sqlite3_column_text");
                errmsg = load<Errmsg>("sqlite3_errmsg");
                available_ = openV2 && close && prepareV2 && step && finalize
                    && columnCount && columnName && columnText && errmsg;
                if (!available_)
                {
                    FreeLibrary(module_);
                    module_ = nullptr;
                }
            }

            ~SqliteApi()
            {
                if (module_) FreeLibrary(module_);
            }

            SqliteApi(const SqliteApi&) = delete;
            SqliteApi& operator=(const SqliteApi&) = delete;

            [[nodiscard]] bool available() const noexcept { return available_; }

            OpenV2 openV2{};
            Close close{};
            PrepareV2 prepareV2{};
            Step step{};
            Finalize finalize{};
            ColumnCount columnCount{};
            ColumnName columnName{};
            ColumnText columnText{};
            Errmsg errmsg{};

        private:
            template<typename Function>
            [[nodiscard]] Function load(const char* name) const noexcept
            {
                return reinterpret_cast<Function>(GetProcAddress(module_, name));
            }

            HMODULE module_{};
            bool available_{ false };
        };

        constexpr int SQLITE_OK_VALUE = 0;
        constexpr int SQLITE_ROW_VALUE = 100;
        constexpr int SQLITE_DONE_VALUE = 101;
        constexpr int SQLITE_OPEN_READONLY_VALUE = 0x00000001;

        class SqliteConnection final
        {
        public:
            SqliteConnection(const SqliteApi& api, const std::filesystem::path& path)
                : api_{ api }
            {
                const std::string utf8Path = utf8FromWide(path.wstring());
                if (api_.openV2(utf8Path.c_str(), &database_, SQLITE_OPEN_READONLY_VALUE, nullptr) != SQLITE_OK_VALUE)
                {
                    std::string message = database_ && api_.errmsg(database_) ? api_.errmsg(database_) : "unknown SQLite open error";
                    if (database_) api_.close(database_);
                    database_ = nullptr;
                    throw std::runtime_error{ "Could not open SQLite database read-only: " + message };
                }
            }

            ~SqliteConnection()
            {
                if (database_) api_.close(database_);
            }

            [[nodiscard]] sqlite3* get() const noexcept { return database_; }

        private:
            const SqliteApi& api_;
            sqlite3* database_{};
        };

        [[nodiscard]] std::vector<DatabaseRowSample> querySqliteRows(
            const SqliteApi& api,
            sqlite3* database,
            const std::string& sql,
            const std::size_t maximumRows)
        {
            sqlite3_stmt* statement{};
            const int prepared = api.prepareV2(database, sql.c_str(), -1, &statement, nullptr);
            if (prepared != SQLITE_OK_VALUE || !statement)
                throw std::runtime_error{ "SQLite query prepare failed: " + std::string{ api.errmsg(database) } };

            std::vector<DatabaseRowSample> rows;
            try
            {
                for (;;)
                {
                    const int state = api.step(statement);
                    if (state == SQLITE_DONE_VALUE) break;
                    if (state != SQLITE_ROW_VALUE)
                        throw std::runtime_error{ "SQLite query failed: " + std::string{ api.errmsg(database) } };
                    if (rows.size() >= maximumRows) break;

                    DatabaseRowSample row;
                    const int count = api.columnCount(statement);
                    for (int column = 0; column < count; ++column)
                    {
                        const char* name = api.columnName(statement, column);
                        const unsigned char* text = api.columnText(statement, column);
                        row.columns.push_back(DatabaseColumnSample{
                            name ? name : "column",
                            text ? reinterpret_cast<const char*>(text) : "NULL"
                        });
                    }
                    rows.push_back(std::move(row));
                }
            }
            catch (...)
            {
                api.finalize(statement);
                throw;
            }
            api.finalize(statement);
            return rows;
        }

        [[nodiscard]] DatabaseInspection inspectSqlite(
            const std::filesystem::path& path,
            const std::size_t maximumObjects,
            const std::size_t sampleRowsPerObject)
        {
            SqliteApi api;
            if (!api.available())
                throw std::runtime_error{ "Windows SQLite runtime is unavailable (winsqlite3.dll/sqlite3.dll)." };
            SqliteConnection connection{ api, path };

            const std::string schemaSql =
                "SELECT type,name,COALESCE(sql,'') AS definition FROM sqlite_master "
                "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' ORDER BY type,name LIMIT "
                + std::to_string(maximumObjects + 1) + ";";
            const auto schemaRows = querySqliteRows(api, connection.get(), schemaSql, maximumObjects + 1);

            DatabaseInspection inspection;
            inspection.family = "SQLite";
            inspection.backend = "Windows WinSQLite/native sqlite3 (read-only)";
            for (const auto& row : schemaRows)
            {
                if (inspection.objects.size() >= maximumObjects)
                {
                    inspection.truncated = true;
                    break;
                }
                DatabaseObject object;
                for (const auto& cell : row.columns)
                {
                    if (cell.name == "type") object.type = cell.value;
                    else if (cell.name == "name") object.name = cell.value;
                    else if (cell.name == "definition") object.definition = cell.value;
                }
                if (!object.name.empty() && sampleRowsPerObject > 0)
                {
                    try
                    {
                        object.sampleRows = querySqliteRows(
                            api, connection.get(),
                            "SELECT * FROM " + quoteSqliteIdentifier(object.name)
                                + " LIMIT " + std::to_string(sampleRowsPerObject) + ";",
                            sampleRowsPerObject);
                    }
                    catch (...) { /* schema remains useful when one object cannot be sampled */ }
                }
                inspection.objects.push_back(std::move(object));
            }
            return inspection;
        }

        [[nodiscard]] DatabaseInspection inspectAccess(
            const std::filesystem::path& path,
            const std::size_t maximumObjects,
            const std::size_t sampleRowsPerObject)
        {
            const auto powershell = resolveExecutable(L"powershell.exe", {});
            if (powershell.empty()) throw std::runtime_error{ "Windows PowerShell is unavailable for Access inspection." };

            TemporaryWorkspace workspace;
            const auto script = workspace.path() / "inspect-access.ps1";
            {
                std::ofstream out{ script, std::ios::binary };
                out << R"PS(param([string]$Path,[int]$MaxObjects,[int]$MaxRows)
$ErrorActionPreference='Stop'
$providers = if ([IO.Path]::GetExtension($Path).ToLowerInvariant() -eq '.mdb') {
  @('Microsoft.ACE.OLEDB.16.0','Microsoft.ACE.OLEDB.12.0','Microsoft.Jet.OLEDB.4.0')
} else { @('Microsoft.ACE.OLEDB.16.0','Microsoft.ACE.OLEDB.12.0') }
$conn=$null; $used=$null; $last=$null
foreach($provider in $providers){
  $candidate=$null
  try { $candidate = New-Object System.Data.OleDb.OleDbConnection("Provider=$provider;Data Source=$Path;Mode=Read;"); $candidate.Open(); $conn=$candidate; $used=$provider; break }
  catch { $last=$_.Exception.Message; if($candidate){$candidate.Dispose()} }
}
if(-not $conn){ throw "No installed read-only ACE/Jet provider could open the database. $last" }
try {
  $tables = $conn.GetSchema('Tables') | Where-Object { ($_.TABLE_TYPE -eq 'TABLE' -or $_.TABLE_TYPE -eq 'VIEW') -and $_.TABLE_NAME -notlike 'MSys*' } | Select-Object -First ($MaxObjects + 1)
  $items=@(); $count=0; $truncated=$false
  foreach($t in $tables){
    if($count -ge $MaxObjects){$truncated=$true; break}; $count++
    $name=[string]$t.TABLE_NAME; $type=[string]$t.TABLE_TYPE; $safe=$name.Replace(']',']]')
    $rows=@()
    if($MaxRows -gt 0){
      $cmd=$conn.CreateCommand(); $cmd.CommandText="SELECT TOP $MaxRows * FROM [$safe]"
      try { $reader=$cmd.ExecuteReader(); while($reader.Read()) { $row=[ordered]@{}; for($i=0;$i -lt $reader.FieldCount;$i++){ $row[$reader.GetName($i)] = if($reader.IsDBNull($i)){$null}else{[string]$reader.GetValue($i)} }; $rows += [pscustomobject]$row }; $reader.Close() } catch {} finally { $cmd.Dispose() }
    }
    $items += [pscustomobject]@{type=$type;name=$name;definition='';sampleRows=$rows}
  }
  [pscustomobject]@{family='Microsoft Access';backend=$used;truncated=$truncated;objects=$items} | ConvertTo-Json -Compress -Depth 8
} finally { $conn.Close(); $conn.Dispose() }
)PS";
            }

            const std::string output = runProcessCapture(powershell, {
                L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File", script.wstring(),
                L"-Path", path.wstring(), L"-MaxObjects", std::to_wstring(maximumObjects),
                L"-MaxRows", std::to_wstring(sampleRowsPerObject)
            });
            const auto root = integrations::json::parse(output);
            DatabaseInspection inspection;
            inspection.family = integrations::json::stringOr(root, "family", "Microsoft Access");
            inspection.backend = integrations::json::stringOr(root, "backend", "ACE/Jet OLE DB");
            inspection.truncated = integrations::json::boolOr(root, "truncated", false);
            const auto* objectsValue = root.find("objects");
            const auto* objects = objectsValue ? objectsValue->array() : nullptr;
            if (!objects) return inspection;
            for (const auto& item : *objects)
            {
                DatabaseObject object;
                object.type = integrations::json::stringOr(item, "type", "TABLE");
                object.name = integrations::json::stringOr(item, "name");
                object.definition = integrations::json::stringOr(item, "definition");
                if (const auto* samples = item.find("sampleRows"))
                    object.sampleRows = parseRows(*samples, sampleRowsPerObject);
                inspection.objects.push_back(std::move(object));
            }
            return inspection;
        }
#endif
    }

    class LocalDatabaseService::Impl
    {
    public:
        [[nodiscard]] bool sqliteAvailable() const noexcept
        {
#ifdef _WIN32
            try { SqliteApi api; return api.available(); }
            catch (...) { return false; }
#else
            return false;
#endif
        }
    };

    LocalDatabaseService::LocalDatabaseService() : impl_{ std::make_unique<Impl>() } {}
    LocalDatabaseService::~LocalDatabaseService() = default;

    bool LocalDatabaseService::availableFor(const std::filesystem::path& path) const noexcept
    {
#ifdef _WIN32
        if (accessExtension(path)) return true; // provider availability is validated on open.
        if (sqliteExtension(path)) return impl_->sqliteAvailable();
#endif
        (void)path;
        return false;
    }

    std::string LocalDatabaseService::availabilityMessage(const std::filesystem::path& path) const
    {
#ifdef _WIN32
        if (accessExtension(path))
            return "Access inspection uses an installed Microsoft ACE/Jet OLE DB provider in read-only mode.";
        if (sqliteExtension(path))
            return impl_->sqliteAvailable()
                ? "SQLite inspection is available through Windows WinSQLite/native sqlite3 in read-only mode."
                : "SQLite inspection requires Windows winsqlite3.dll or a compatible sqlite3.dll in tools/sqlite/PATH.";
        return "Rose recognizes this database extension but Batch 33 has no read-only backend for it.";
#else
        (void)path;
        return "Local database inspection in Batch 33 is currently implemented for Windows.";
#endif
    }

    DatabaseInspection LocalDatabaseService::inspect(
        const std::filesystem::path& path,
        const std::size_t maximumObjects,
        const std::size_t sampleRowsPerObject) const
    {
#ifndef _WIN32
        (void)sampleRowsPerObject;
#endif
        if (!path.is_absolute()) throw std::invalid_argument{ "Database inspection requires an absolute file path." };
        if (maximumObjects == 0) throw std::invalid_argument{ "Database inspection requires maximumObjects > 0." };
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
            throw std::runtime_error{ "Database file does not exist or is not readable: " + path.string() };
#ifdef _WIN32
        if (accessExtension(path)) return inspectAccess(path, maximumObjects, sampleRowsPerObject);
        if (sqliteExtension(path))
        {
            if (!hasSqliteHeader(path))
                throw std::runtime_error{ "The selected database file does not contain a SQLite file header." };
            return inspectSqlite(path, maximumObjects, sampleRowsPerObject);
        }
#endif
        throw std::runtime_error{ availabilityMessage(path) };
    }
}
