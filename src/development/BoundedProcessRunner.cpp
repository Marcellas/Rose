#include "development/BoundedProcessRunner.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace rose::development
{
    namespace
    {
        class BoundedOutput final
        {
        public:
            BoundedOutput(
                const std::size_t maximumBytes,
                const std::size_t preservedHeadBytes,
                std::string operationName)
                : maximumBytes_{ maximumBytes }
                , preservedHeadBytes_{ preservedHeadBytes }
                , operationName_{ std::move(operationName) }
            {
                if (maximumBytes_ == 0)
                    throw std::invalid_argument{ "Bounded process output limit must be greater than zero." };
                if (preservedHeadBytes_ > maximumBytes_)
                    throw std::invalid_argument{ "Bounded process preserved head cannot exceed total output limit." };
            }

            void append(const char* data, const std::size_t size)
            {
                if (size == 0) return;

                totalBytes_ += size;
                if (head_.size() < preservedHeadBytes_)
                {
                    const std::size_t copyCount =
                        std::min(size, preservedHeadBytes_ - head_.size());
                    head_.append(data, copyCount);
                    data += copyCount;
                    if (copyCount == size) return;
                    appendTail(data, size - copyCount);
                    return;
                }

                appendTail(data, size);
            }

            [[nodiscard]] bool truncated() const noexcept
            {
                return totalBytes_ > maximumBytes_;
            }

            [[nodiscard]] std::string text() const
            {
                if (!truncated())
                    return head_ + tail_;

                std::string result = head_;
                result += "\n... Rose truncated ";
                result += operationName_;
                result += " output; preserving the beginning and most recent diagnostics ...\n";
                result += tail_;
                return result;
            }

        private:
            void appendTail(const char* data, const std::size_t size)
            {
                const std::size_t tailLimit = maximumBytes_ - preservedHeadBytes_;
                if (tailLimit == 0) return;

                if (size >= tailLimit)
                {
                    tail_.assign(data + (size - tailLimit), tailLimit);
                    return;
                }

                if (tail_.size() + size > tailLimit)
                {
                    const std::size_t removeCount =
                        tail_.size() + size - tailLimit;
                    tail_.erase(0, removeCount);
                }

                tail_.append(data, size);
            }

            std::size_t maximumBytes_{};
            std::size_t preservedHeadBytes_{};
            std::string operationName_;
            std::string head_;
            std::string tail_;
            std::size_t totalBytes_{ 0 };
        };

#ifdef _WIN32
        class UniqueHandle final
        {
        public:
            explicit UniqueHandle(HANDLE value = nullptr) noexcept
                : value_{ value }
            {
            }

            ~UniqueHandle()
            {
                reset();
            }

            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;

            UniqueHandle(UniqueHandle&& other) noexcept
                : value_{ std::exchange(other.value_, nullptr) }
            {
            }

            UniqueHandle& operator=(UniqueHandle&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    value_ = std::exchange(other.value_, nullptr);
                }
                return *this;
            }

            [[nodiscard]] HANDLE get() const noexcept { return value_; }

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
            }

            void reset(HANDLE replacement = nullptr) noexcept
            {
                if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE)
                    CloseHandle(value_);
                value_ = replacement;
            }

        private:
            HANDLE value_{ nullptr };
        };

        [[nodiscard]]
        std::wstring quoteWindowsArgument(const std::wstring_view argument)
        {
            if (argument.empty()) return L"\"\"";

            const bool needsQuotes =
                argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
            if (!needsQuotes) return std::wstring{ argument };

            std::wstring result{ L"\"" };
            std::size_t backslashes{ 0 };

            for (const wchar_t character : argument)
            {
                if (character == L'\\')
                {
                    ++backslashes;
                    continue;
                }

                if (character == L'\"')
                {
                    result.append(backslashes * 2 + 1, L'\\');
                    result.push_back(L'\"');
                    backslashes = 0;
                    continue;
                }

                result.append(backslashes, L'\\');
                backslashes = 0;
                result.push_back(character);
            }

            result.append(backslashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }

        [[nodiscard]]
        std::wstring makeCommandLine(const BoundedProcessRequest& request)
        {
            std::wstring commandLine = quoteWindowsArgument(request.executable.wstring());
            for (const std::wstring& argument : request.arguments)
            {
                commandLine.push_back(L' ');
                commandLine += quoteWindowsArgument(argument);
            }
            return commandLine;
        }

        void drainPipe(HANDLE pipe, BoundedOutput& output, const std::string& operationName)
        {
            std::array<char, 4096> buffer{};

            while (true)
            {
                DWORD available{};
                if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) == FALSE)
                {
                    const DWORD error = GetLastError();
                    if (error == ERROR_BROKEN_PIPE) return;
                    throw std::runtime_error{
                        "Could not inspect " + operationName
                        + " output pipe. Win32 error=" + std::to_string(error)
                    };
                }

                if (available == 0) return;

                DWORD readCount{};
                const DWORD desired =
                    std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));

                if (ReadFile(pipe, buffer.data(), desired, &readCount, nullptr) == FALSE)
                {
                    const DWORD error = GetLastError();
                    if (error == ERROR_BROKEN_PIPE) return;
                    throw std::runtime_error{
                        "Could not read " + operationName
                        + " output pipe. Win32 error=" + std::to_string(error)
                    };
                }

                output.append(buffer.data(), static_cast<std::size_t>(readCount));
            }
        }
#endif
    }

    std::filesystem::path findExecutableOnPath(
        const std::wstring_view executableName)
    {
        if (executableName.empty())
            throw std::invalid_argument{ "Executable name cannot be empty." };

#ifdef _WIN32
        std::array<wchar_t, 32768> buffer{};
        const std::wstring executable{ executableName };
        const DWORD length = SearchPathW(
            nullptr,
            executable.c_str(),
            nullptr,
            static_cast<DWORD>(buffer.size()),
            buffer.data(),
            nullptr);

        if (length == 0 || length >= buffer.size())
            throw std::runtime_error{ "Required executable was not found on PATH." };

        return std::filesystem::path{ std::wstring{ buffer.data(), length } };
#else
        (void)executableName;
        throw std::runtime_error{ "Executable PATH resolution currently requires Windows." };
#endif
    }

    BoundedProcessResult runBoundedProcess(
        const BoundedProcessRequest& request)
    {
        if (request.executable.empty() || !request.executable.is_absolute())
            throw std::invalid_argument{ "Bounded process executable must be an absolute path." };
        if (request.workingDirectory.empty() || !request.workingDirectory.is_absolute())
            throw std::invalid_argument{ "Bounded process working directory must be an absolute path." };
        if (request.timeout <= std::chrono::milliseconds{ 0 })
            throw std::invalid_argument{ "Bounded process timeout must be greater than zero." };
        if (request.operationName.empty())
            throw std::invalid_argument{ "Bounded process operation name cannot be empty." };

#ifdef _WIN32
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;

        HANDLE rawRead{};
        HANDLE rawWrite{};
        if (CreatePipe(&rawRead, &rawWrite, &attributes, 0) == FALSE)
        {
            throw std::runtime_error{
                "Could not create " + request.operationName
                + " output pipe. Win32 error=" + std::to_string(GetLastError())
            };
        }

        UniqueHandle readPipe{ rawRead };
        UniqueHandle writePipe{ rawWrite };
        if (SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
            throw std::runtime_error{ "Could not protect child output read handle from inheritance." };

        UniqueHandle job{ CreateJobObjectW(nullptr, nullptr) };
        if (!job)
        {
            throw std::runtime_error{
                "Could not create " + request.operationName
                + " job object. Win32 error=" + std::to_string(GetLastError())
            };
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(
                job.get(),
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits)) == FALSE)
        {
            throw std::runtime_error{
                "Could not configure " + request.operationName
                + " job object. Win32 error=" + std::to_string(GetLastError())
            };
        }

        UniqueHandle nullInput{
            CreateFileW(
                L"NUL",
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                &attributes,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr)
        };
        if (!nullInput)
            throw std::runtime_error{ "Could not open NUL for non-interactive child input." };

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = writePipe.get();
        startup.hStdError = writePipe.get();
        startup.hStdInput = nullInput.get();

        PROCESS_INFORMATION processInfo{};
        std::wstring commandLine = makeCommandLine(request);
        const std::wstring application = request.executable.wstring();
        const std::wstring workingDirectory = request.workingDirectory.wstring();

        const BOOL created = CreateProcessW(
            application.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.c_str(),
            &startup,
            &processInfo);

        if (created == FALSE)
        {
            throw std::runtime_error{
                "Windows could not start " + request.operationName
                + ". Win32 error=" + std::to_string(GetLastError())
            };
        }

        UniqueHandle process{ processInfo.hProcess };
        UniqueHandle thread{ processInfo.hThread };

        if (AssignProcessToJobObject(job.get(), process.get()) == FALSE)
        {
            const DWORD assignmentError = GetLastError();
            (void)TerminateProcess(process.get(), 125);
            throw std::runtime_error{
                "Could not assign " + request.operationName
                + " to its bounded job object. Win32 error="
                + std::to_string(assignmentError)
            };
        }

        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1))
        {
            (void)TerminateJobObject(job.get(), 125);
            throw std::runtime_error{
                "Could not resume " + request.operationName
                + " process. Win32 error=" + std::to_string(GetLastError())
            };
        }

        writePipe.reset();

        BoundedOutput captured{
            request.maximumCapturedBytes,
            request.preservedHeadBytes,
            request.operationName
        };

        const auto deadline = std::chrono::steady_clock::now() + request.timeout;
        bool timedOut = false;

        while (true)
        {
            drainPipe(readPipe.get(), captured, request.operationName);

            const DWORD wait = WaitForSingleObject(process.get(), 50);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_FAILED)
            {
                throw std::runtime_error{
                    "Waiting for " + request.operationName
                    + " failed. Win32 error=" + std::to_string(GetLastError())
                };
            }

            if (std::chrono::steady_clock::now() >= deadline)
            {
                timedOut = true;
                if (TerminateJobObject(job.get(), 124) == FALSE)
                {
                    throw std::runtime_error{
                        request.operationName
                        + " timed out and Rose could not terminate its child job. Win32 error="
                        + std::to_string(GetLastError())
                    };
                }
                (void)WaitForSingleObject(process.get(), 5000);
                break;
            }
        }

        drainPipe(readPipe.get(), captured, request.operationName);

        DWORD exitCode{};
        if (GetExitCodeProcess(process.get(), &exitCode) == FALSE)
        {
            throw std::runtime_error{
                "Could not query " + request.operationName
                + " exit code. Win32 error=" + std::to_string(GetLastError())
            };
        }

        return BoundedProcessResult{
            .exitCode = static_cast<std::uint32_t>(exitCode),
            .timedOut = timedOut,
            .outputTruncated = captured.truncated(),
            .output = captured.text()
        };
#else
        (void)request;
        throw std::runtime_error{ "Bounded child process execution currently requires Windows." };
#endif
    }
}
