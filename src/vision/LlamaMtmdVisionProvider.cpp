#include "vision/LlamaMtmdVisionProvider.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rose::vision
{

    namespace
    {

        [[nodiscard]]
        std::string readTextFile(
            const std::filesystem::path& path)
        {
            std::ifstream stream{
                path,
                std::ios::binary
            };

            if (!stream)
            {
                return {};
            }

            std::ostringstream buffer;
            buffer << stream.rdbuf();
            return buffer.str();
        }


        void writeTextFile(
            const std::filesystem::path& path,
            const std::string_view text)
        {
            std::ofstream stream{
                path,
                std::ios::binary | std::ios::trunc
            };

            if (!stream)
            {
                throw std::runtime_error{
                    "Could not create Rose vision temporary text file: "
                    + path.string()
                };
            }

            stream.write(
                text.data(),
                static_cast<std::streamsize>(
                    text.size()));

            if (!stream)
            {
                throw std::runtime_error{
                    "Could not write Rose vision temporary text file: "
                    + path.string()
                };
            }
        }


        void writeBinaryFile(
            const std::filesystem::path& path,
            const std::span<const std::uint8_t> bytes)
        {
            std::ofstream stream{
                path,
                std::ios::binary | std::ios::trunc
            };

            if (!stream)
            {
                throw std::runtime_error{
                    "Could not create Rose vision temporary image file: "
                    + path.string()
                };
            }

            if (!bytes.empty())
            {
                stream.write(
                    reinterpret_cast<const char*>(
                        bytes.data()),
                    static_cast<std::streamsize>(
                        bytes.size()));
            }

            if (!stream)
            {
                throw std::runtime_error{
                    "Could not write Rose vision temporary image file: "
                    + path.string()
                };
            }
        }


        [[nodiscard]]
        std::string normalizedImageExtension(
            std::string_view extension)
        {
            std::string result;
            result.reserve(extension.size() + 1);

            if (
                extension.empty()
                || extension.front() != '.')
            {
                result.push_back('.');
            }

            for (const char character : extension)
            {
                const unsigned char value =
                    static_cast<unsigned char>(character);

                if (
                    character == '.'
                    || std::isalnum(value) != 0)
                {
                    result.push_back(
                        static_cast<char>(
                            std::tolower(value)));
                }
            }

            if (
                result.size() < 2
                || result.size() > 12)
            {
                return ".img";
            }

            return result;
        }


        void truncateUtf8Prefix(
            std::string& text,
            const std::size_t maximumBytes)
        {
            if (text.size() <= maximumBytes)
            {
                return;
            }

            text.resize(maximumBytes);

            // If the byte immediately after our retained prefix would have been a
            // UTF-8 continuation byte, the prefix may now end inside a code point.
            // Remove continuation bytes and then remove the leading byte for that
            // incomplete code point as well.
            while (
                !text.empty()
                && (
                    static_cast<unsigned char>(
                        text.back())
                    & 0xC0u)
                    == 0x80u)
            {
                text.pop_back();
            }

            if (!text.empty())
            {
                const unsigned char last =
                    static_cast<unsigned char>(
                        text.back());

                if (
                    (last & 0xE0u) == 0xC0u
                    || (last & 0xF0u) == 0xE0u
                    || (last & 0xF8u) == 0xF0u)
                {
                    text.pop_back();
                }
            }
        }


#ifdef _WIN32

        [[nodiscard]]
        std::filesystem::path executableDirectory()
        {
            std::vector<wchar_t> buffer(32768);

            const DWORD written =
                GetModuleFileNameW(
                    nullptr,
                    buffer.data(),
                    static_cast<DWORD>(
                        buffer.size()));

            if (
                written == 0
                || written >= buffer.size())
            {
                return {};
            }

            return std::filesystem::path{
                std::wstring{
                    buffer.data(),
                    written
                }
            }.parent_path();
        }


        [[nodiscard]]
        std::wstring quoteWindowsArgument(
            const std::wstring& argument)
        {
            if (
                argument.find_first_of(L" \t\n\v\"")
                == std::wstring::npos)
            {
                return argument;
            }

            std::wstring result;
            result.push_back(L'\"');

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
                    result.append(
                        backslashes * 2 + 1,
                        L'\\');
                    result.push_back(L'\"');
                    backslashes = 0;
                    continue;
                }

                result.append(
                    backslashes,
                    L'\\');
                backslashes = 0;
                result.push_back(character);
            }

            result.append(
                backslashes * 2,
                L'\\');
            result.push_back(L'\"');

            return result;
        }


        class ProcessHandles final
        {
        public:
            ~ProcessHandles()
            {
                if (thread_ != nullptr)
                {
                    CloseHandle(thread_);
                }

                if (process_ != nullptr)
                {
                    CloseHandle(process_);
                }
            }

            HANDLE process_{ nullptr };
            HANDLE thread_{ nullptr };
        };

#endif


        class TemporaryFiles final
        {
        public:
            ~TemporaryFiles()
            {
                std::error_code error;

                for (const auto& path : files_)
                {
                    std::filesystem::remove(
                        path,
                        error);
                    error.clear();
                }
            }

            void add(
                std::filesystem::path path)
            {
                files_.push_back(
                    std::move(path));
            }

        private:
            std::vector<std::filesystem::path> files_;
        };


        [[nodiscard]]
        std::filesystem::path resolveWorkerExecutable(
            const LlamaMtmdVisionConfig& config)
        {
            if (!config.workerExecutable.empty())
            {
                return config.workerExecutable;
            }

#ifdef _WIN32
            const auto moduleDirectory =
                executableDirectory();

            if (!moduleDirectory.empty())
            {
                const auto besideRose =
                    moduleDirectory
                    / "RoseVisionWorker.exe";

                if (
                    std::filesystem::exists(
                        besideRose))
                {
                    return besideRose;
                }
            }
#endif

            static constexpr const char* candidates[]{
                "build/Debug/RoseVisionWorker.exe",
                "build/Release/RoseVisionWorker.exe",
                "RoseVisionWorker.exe"
            };

            for (const char* candidate : candidates)
            {
                const std::filesystem::path path{
                    candidate
                };

                if (std::filesystem::exists(path))
                {
                    return path;
                }
            }

            return {};
        }

    } // namespace


    struct LlamaMtmdVisionProvider::Impl
    {
        explicit Impl(
            LlamaMtmdVisionConfig config)
            : config_{ std::move(config) }
        {
        }


        [[nodiscard]]
        bool available() const noexcept
        {
            try
            {
#ifdef _WIN32
                const auto worker =
                    resolveWorkerExecutable(config_);

                return
                    !worker.empty()
                    && std::filesystem::exists(worker)
                    && std::filesystem::exists(
                        config_.modelPath)
                    && std::filesystem::exists(
                        config_.mmprojPath);
#else
                return false;
#endif
            }
            catch (...)
            {
                return false;
            }
        }


        [[nodiscard]]
        std::string availabilityMessage() const
        {
#ifndef _WIN32
            return "The first Rose llama.cpp vision worker currently supports Windows only.";
#else
            const auto worker =
                resolveWorkerExecutable(config_);

            if (worker.empty())
            {
                return "RoseVisionWorker.exe was not found beside Rose or in the normal build output folders.";
            }

            if (!std::filesystem::exists(
                    config_.modelPath))
            {
                return "Vision GGUF model not found: "
                    + config_.modelPath.string();
            }

            if (!std::filesystem::exists(
                    config_.mmprojPath))
            {
                return "Vision mmproj model not found: "
                    + config_.mmprojPath.string();
            }

            return "Local llama.cpp semantic vision is available.";
#endif
        }


        [[nodiscard]]
        VisionResult analyze(
            const VisionRequest& request)
        {
            if (request.encodedImage.empty())
            {
                throw std::invalid_argument{
                    "VisionRequest contains no image bytes."
                };
            }

            if (!available())
            {
                throw std::runtime_error{
                    availabilityMessage()
                };
            }

#ifndef _WIN32
            throw std::runtime_error{
                "Rose's first llama.cpp vision worker currently supports Windows only."
            };
#else
            const std::filesystem::path worker =
                resolveWorkerExecutable(config_);

            const std::filesystem::path tempDirectory =
                std::filesystem::temp_directory_path()
                / "Rose"
                / "vision";

            std::filesystem::create_directories(
                tempDirectory);

            static std::atomic<std::uint64_t> nextId{
                0
            };

            const std::uint64_t id =
                nextId.fetch_add(
                    1,
                    std::memory_order_relaxed);

            const std::string stem =
                "rose_vision_"
                + std::to_string(
                    GetCurrentProcessId())
                + "_"
                + std::to_string(id);

            const std::filesystem::path imagePath =
                tempDirectory
                / (stem
                   + normalizedImageExtension(
                       request.sourceExtension));

            const std::filesystem::path promptPath =
                tempDirectory
                / (stem + ".prompt.txt");

            const std::filesystem::path outputPath =
                tempDirectory
                / (stem + ".result.txt");

            const std::filesystem::path errorPath =
                tempDirectory
                / (stem + ".error.txt");

            TemporaryFiles cleanup;
            cleanup.add(imagePath);
            cleanup.add(promptPath);
            cleanup.add(outputPath);
            cleanup.add(errorPath);

            writeBinaryFile(
                imagePath,
                request.encodedImage);

            const std::string prompt =
                request.userPrompt.empty()
                    ? std::string{
                        "Describe the image accurately and identify the most important visible details."
                    }
                    : std::string{
                        request.userPrompt
                    };

            writeTextFile(
                promptPath,
                prompt);

            std::wstring commandLine;

            const auto appendArgument =
                [&commandLine](
                    const std::wstring& argument)
                {
                    if (!commandLine.empty())
                    {
                        commandLine.push_back(L' ');
                    }

                    commandLine +=
                        quoteWindowsArgument(argument);
                };

            appendArgument(
                worker.wstring());

            appendArgument(L"--model");
            appendArgument(
                std::filesystem::absolute(
                    config_.modelPath).wstring());

            appendArgument(L"--mmproj");
            appendArgument(
                std::filesystem::absolute(
                    config_.mmprojPath).wstring());

            appendArgument(L"--image");
            appendArgument(imagePath.wstring());

            appendArgument(L"--prompt-file");
            appendArgument(promptPath.wstring());

            appendArgument(L"--output-file");
            appendArgument(outputPath.wstring());

            appendArgument(L"--error-file");
            appendArgument(errorPath.wstring());

            appendArgument(L"--context");
            appendArgument(
                std::to_wstring(
                    config_.contextSize));

            appendArgument(L"--predict");
            appendArgument(
                std::to_wstring(
                    config_.maxGeneratedTokens));

            appendArgument(L"--gpu-layers");
            appendArgument(
                std::to_wstring(
                    config_.gpuLayers));

            appendArgument(L"--mmproj-gpu");
            appendArgument(
                config_.mmprojUseGpu
                    ? L"1"
                    : L"0");

            appendArgument(L"--image-max-tokens");
            appendArgument(
                std::to_wstring(
                    config_.imageMaxTokens));

            std::vector<wchar_t> mutableCommand(
                commandLine.begin(),
                commandLine.end());
            mutableCommand.push_back(L'\0');

            STARTUPINFOW startupInfo{};
            startupInfo.cb =
                sizeof(startupInfo);

            PROCESS_INFORMATION processInformation{};

            // Make the wide executable path explicit rather than relying on
            // std::filesystem::path::value_type being wchar_t on Windows.
            const std::wstring workerPath =
                worker.wstring();

            const BOOL created =
                CreateProcessW(
                    workerPath.c_str(),
                    mutableCommand.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    nullptr,
                    &startupInfo,
                    &processInformation);

            if (!created)
            {
                throw std::runtime_error{
                    "Could not start RoseVisionWorker.exe. Windows error: "
                    + std::to_string(
                        GetLastError())
                };
            }

            ProcessHandles handles;
            handles.process_ =
                processInformation.hProcess;
            handles.thread_ =
                processInformation.hThread;

            const auto timeoutCount =
                config_.timeout.count();

            const DWORD waitMilliseconds =
                timeoutCount >= static_cast<long long>(INFINITE - 1)
                    ? INFINITE - 1
                    : static_cast<DWORD>(
                        timeoutCount);

            const DWORD waitResult =
                WaitForSingleObject(
                    handles.process_,
                    waitMilliseconds);

            if (waitResult == WAIT_TIMEOUT)
            {
                TerminateProcess(
                    handles.process_,
                    124);

                WaitForSingleObject(
                    handles.process_,
                    5000);

                throw std::runtime_error{
                    "Local semantic vision timed out while analyzing the image."
                };
            }

            if (waitResult != WAIT_OBJECT_0)
            {
                throw std::runtime_error{
                    "Waiting for RoseVisionWorker failed. Windows error: "
                    + std::to_string(
                        GetLastError())
                };
            }

            DWORD exitCode{ 1 };

            if (!GetExitCodeProcess(
                    handles.process_,
                    &exitCode))
            {
                throw std::runtime_error{
                    "Could not read RoseVisionWorker exit status."
                };
            }

            if (exitCode != 0)
            {
                std::string error =
                    readTextFile(errorPath);

                if (error.empty())
                {
                    error =
                        "RoseVisionWorker failed with exit code "
                        + std::to_string(exitCode)
                        + ".";
                }

                throw std::runtime_error{
                    std::move(error)
                };
            }

            std::string output =
                readTextFile(outputPath);

            if (output.empty())
            {
                throw std::runtime_error{
                    "The local vision model returned no text."
                };
            }

            VisionResult result;
            result.truncated =
                output.size()
                > config_.maximumOutputUtf8Bytes;

            if (result.truncated)
            {
                truncateUtf8Prefix(
                    output,
                    config_.maximumOutputUtf8Bytes);
            }

            result.text =
                std::move(output);

            return result;
#endif
        }


        LlamaMtmdVisionConfig config_;
    };


    LlamaMtmdVisionProvider::LlamaMtmdVisionProvider(
        LlamaMtmdVisionConfig config)
        : impl_{
            std::make_unique<Impl>(
                std::move(config))
        }
    {
    }


    LlamaMtmdVisionProvider::~LlamaMtmdVisionProvider() = default;


    bool LlamaMtmdVisionProvider::available() const noexcept
    {
        return impl_->available();
    }


    std::string LlamaMtmdVisionProvider::availabilityMessage() const
    {
        return impl_->availabilityMessage();
    }


    VisionResult LlamaMtmdVisionProvider::analyze(
        const VisionRequest& request)
    {
        return impl_->analyze(request);
    }

} // namespace rose::vision
