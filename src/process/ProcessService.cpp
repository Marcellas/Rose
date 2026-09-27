#include "process/ProcessService.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#endif

namespace rose::process
{
    namespace
    {
        [[nodiscard]] std::string lowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        void validateExecutablePath(const std::filesystem::path& path)
        {
            if (!path.is_absolute())
                throw std::invalid_argument{ "Program launch requires an absolute executable path." };
            if (lowerAscii(path.extension().string()) != ".exe")
                throw std::invalid_argument{ "Program launch currently supports .exe targets only." };
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error)
                throw std::runtime_error{ "Executable does not exist or is not readable: " + path.string() };
        }

#ifdef _WIN32
        class UniqueHandle final
        {
        public:
            explicit UniqueHandle(HANDLE handle = nullptr) noexcept : handle_{ handle } {}
            ~UniqueHandle() { reset(); }
            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;
            UniqueHandle(UniqueHandle&& other) noexcept : handle_{ std::exchange(other.handle_, nullptr) } {}
            UniqueHandle& operator=(UniqueHandle&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    handle_ = std::exchange(other.handle_, nullptr);
                }
                return *this;
            }
            [[nodiscard]] HANDLE get() const noexcept { return handle_; }
            [[nodiscard]] explicit operator bool() const noexcept
            {
                return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
            }
            void reset(HANDLE replacement = nullptr) noexcept
            {
                if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
                handle_ = replacement;
            }
        private:
            HANDLE handle_{ nullptr };
        };

        [[nodiscard]] std::wstring quoteExecutable(const std::wstring& argument)
        {
            std::wstring result{ L"\"" };
            std::size_t backslashes{};
            for (const wchar_t ch : argument)
            {
                if (ch == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (ch == L'\"')
                {
                    result.append(backslashes * 2 + 1, L'\\');
                    result.push_back(L'\"');
                    backslashes = 0;
                    continue;
                }
                result.append(backslashes, L'\\');
                backslashes = 0;
                result.push_back(ch);
            }
            result.append(backslashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }

        [[nodiscard]] std::wstring wideFromUtf8(const std::string& value)
        {
            if (value.empty()) return {};
            const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (required <= 0) throw std::invalid_argument{ "Program arguments are not valid UTF-8." };
            std::wstring result(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                value.data(), static_cast<int>(value.size()), result.data(), required) <= 0)
                throw std::invalid_argument{ "Program arguments are not valid UTF-8." };
            return result;
        }

        [[nodiscard]] std::string utf8FromWide(const std::wstring_view value)
        {
            if (value.empty()) return {};
            const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (required <= 0) return {};
            std::string result(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                result.data(), required, nullptr, nullptr) <= 0) return {};
            return result;
        }

        [[nodiscard]] std::string queryImagePath(const DWORD processId)
        {
            UniqueHandle process{ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId) };
            if (!process) return {};
            std::wstring buffer(32768, L'\0');
            DWORD length = static_cast<DWORD>(buffer.size());
            if (QueryFullProcessImageNameW(process.get(), 0, buffer.data(), &length) == FALSE) return {};
            buffer.resize(length);
            return utf8FromWide(buffer);
        }

        struct WindowQuery
        {
            DWORD processId{};
            std::size_t count{};
            bool postClose{ false };
        };

        BOOL CALLBACK enumerateWindows(HWND window, LPARAM parameter)
        {
            auto* query = reinterpret_cast<WindowQuery*>(parameter);
            DWORD owner{};
            (void)GetWindowThreadProcessId(window, &owner);
            if (owner != query->processId) return TRUE;
            if (GetWindow(window, GW_OWNER) != nullptr || IsWindowVisible(window) == FALSE) return TRUE;
            if (!query->postClose)
            {
                ++query->count;
            }
            else if (PostMessageW(window, WM_CLOSE, 0, 0) != FALSE)
            {
                ++query->count;
            }
            return TRUE;
        }

        [[nodiscard]] std::size_t topLevelWindows(const DWORD processId, const bool postClose)
        {
            WindowQuery query{ .processId = processId, .count = 0, .postClose = postClose };
            if (EnumWindows(enumerateWindows, reinterpret_cast<LPARAM>(&query)) == FALSE)
                throw std::runtime_error{ "Windows could not enumerate top-level windows for the process." };
            return query.count;
        }
#endif
    }

    LaunchProcessResult LocalProcessService::launch(const LaunchProcessRequest& request)
    {
        validateExecutablePath(request.executablePath);
        if (request.workingDirectory.has_value())
        {
            if (!request.workingDirectory->is_absolute())
                throw std::invalid_argument{ "Program working directory must be absolute when supplied." };
            std::error_code error;
            if (!std::filesystem::is_directory(*request.workingDirectory, error) || error)
                throw std::runtime_error{ "Program working directory does not exist: " + request.workingDirectory->string() };
        }

#ifdef _WIN32
        const std::filesystem::path normalized = request.executablePath.lexically_normal();
        const std::wstring application = normalized.wstring();
        std::wstring commandLine = quoteExecutable(application);
        if (!request.arguments.empty())
        {
            commandLine.push_back(L' ');
            commandLine += wideFromUtf8(request.arguments);
        }
        std::wstring workingDirectory;
        if (request.workingDirectory.has_value()) workingDirectory = request.workingDirectory->lexically_normal().wstring();

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION processInfo{};
        const BOOL created = CreateProcessW(application.c_str(), commandLine.data(), nullptr, nullptr,
            FALSE, 0, nullptr,
            request.workingDirectory.has_value() ? workingDirectory.c_str() : nullptr,
            &startup, &processInfo);
        if (created == FALSE)
            throw std::runtime_error{ "Windows could not launch the executable. Win32 error=" + std::to_string(GetLastError()) };

        UniqueHandle process{ processInfo.hProcess };
        UniqueHandle thread{ processInfo.hThread };
        return LaunchProcessResult{
            .processId = static_cast<std::uint32_t>(processInfo.dwProcessId),
            .executablePath = normalized
        };
#else
        throw std::runtime_error{ "Program launch is currently implemented for Windows only." };
#endif
    }

    std::vector<ProcessInfo> LocalProcessService::listProcesses(const std::size_t maxResults) const
    {
        if (maxResults == 0) return {};
#ifdef _WIN32
        UniqueHandle snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if (!snapshot) throw std::runtime_error{ "Windows could not snapshot running processes." };

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot.get(), &entry) == FALSE)
            throw std::runtime_error{ "Windows could not enumerate running processes." };

        std::vector<ProcessInfo> result;
        result.reserve((std::min)(maxResults, static_cast<std::size_t>(128)));
        do
        {
            ProcessInfo info;
            info.processId = static_cast<std::uint32_t>(entry.th32ProcessID);
            info.executableName = utf8FromWide(entry.szExeFile);
            info.imagePath = queryImagePath(entry.th32ProcessID);
            try { info.hasTopLevelWindow = topLevelWindows(entry.th32ProcessID, false) > 0; }
            catch (...) { info.hasTopLevelWindow = false; }
            result.push_back(std::move(info));
            if (result.size() >= maxResults) break;
        }
        while (Process32NextW(snapshot.get(), &entry) != FALSE);

        std::sort(result.begin(), result.end(), [](const ProcessInfo& left, const ProcessInfo& right)
        {
            if (left.executableName != right.executableName) return left.executableName < right.executableName;
            return left.processId < right.processId;
        });
        return result;
#else
        throw std::runtime_error{ "Process listing is currently implemented for Windows only." };
#endif
    }

    CloseProcessResult LocalProcessService::requestClose(const std::uint32_t processId)
    {
        if (processId == 0) throw std::invalid_argument{ "close_process requires a non-zero process id." };
#ifdef _WIN32
        if (processId == static_cast<std::uint32_t>(GetCurrentProcessId()))
            throw std::invalid_argument{ "Rose will not close her own process." };
        if (processId <= 4)
            throw std::invalid_argument{ "Rose will not request closure of Windows system process ids 0-4." };

        UniqueHandle process{ OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, static_cast<DWORD>(processId)) };
        if (!process)
            throw std::runtime_error{ "Could not open process " + std::to_string(processId) + " for graceful close/status inspection." };

        const std::string imagePath = queryImagePath(static_cast<DWORD>(processId));
        const std::string executableName = imagePath.empty()
            ? std::string{}
            : std::filesystem::path{ imagePath }.filename().string();

        const std::size_t windows = topLevelWindows(static_cast<DWORD>(processId), true);
        if (windows == 0)
            throw std::runtime_error{ "Process " + std::to_string(processId)
                + " has no visible top-level window to close gracefully. Rose did not terminate it." };

        const DWORD waitResult = WaitForSingleObject(process.get(), 1500);
        return CloseProcessResult{
            .processId = processId,
            .executableName = executableName,
            .imagePath = imagePath,
            .windowsNotified = windows,
            .exited = waitResult == WAIT_OBJECT_0
        };
#else
        throw std::runtime_error{ "Graceful process close is currently implemented for Windows only." };
#endif
    }
}
