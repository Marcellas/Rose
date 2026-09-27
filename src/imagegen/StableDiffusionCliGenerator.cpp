#include "imagegen/StableDiffusionCliGenerator.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rose::imagegen
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
        std::wstring utf8ToWide(
            const std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int required =
                MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    text.data(),
                    static_cast<int>(text.size()),
                    nullptr,
                    0);

            if (required <= 0)
            {
                throw std::runtime_error{
                    "Could not convert UTF-8 text for the Windows image-generation process."
                };
            }

            std::wstring result(
                static_cast<std::size_t>(required),
                L'\0');

            const int written =
                MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    text.data(),
                    static_cast<int>(text.size()),
                    result.data(),
                    required);

            if (written != required)
            {
                throw std::runtime_error{
                    "Could not finish UTF-8 conversion for the Windows image-generation process."
                };
            }

            return result;
        }


        [[nodiscard]]
        std::filesystem::path searchPathExecutable(
            const wchar_t* name)
        {
            const DWORD required =
                SearchPathW(
                    nullptr,
                    name,
                    nullptr,
                    0,
                    nullptr,
                    nullptr);

            if (required == 0)
            {
                return {};
            }

            std::vector<wchar_t> buffer(
                static_cast<std::size_t>(required) + 1);

            const DWORD written =
                SearchPathW(
                    nullptr,
                    name,
                    nullptr,
                    static_cast<DWORD>(buffer.size()),
                    buffer.data(),
                    nullptr);

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
            };
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


        class Handle final
        {
        public:
            Handle() = default;

            explicit Handle(
                HANDLE handle) noexcept
                : handle_{ handle }
            {
            }

            ~Handle()
            {
                reset();
            }

            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;

            Handle(Handle&& other) noexcept
                : handle_{ std::exchange(other.handle_, nullptr) }
            {
            }

            Handle& operator=(Handle&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    handle_ =
                        std::exchange(
                            other.handle_,
                            nullptr);
                }

                return *this;
            }

            void reset(
                HANDLE handle = nullptr) noexcept
            {
                if (
                    handle_ != nullptr
                    && handle_ != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle_);
                }

                handle_ = handle;
            }

            [[nodiscard]]
            HANDLE get() const noexcept
            {
                return handle_;
            }

        private:
            HANDLE handle_{ nullptr };
        };
#endif


        [[nodiscard]]
        std::filesystem::path resolveExecutable(
            const StableDiffusionCliConfig& config)
        {
            if (!config.executablePath.empty())
            {
                return config.executablePath;
            }

#ifdef _WIN32
            const std::filesystem::path moduleDirectory =
                executableDirectory();

            if (!moduleDirectory.empty())
            {
                const std::filesystem::path besideRose =
                    moduleDirectory
                    / "sd-cli.exe";

                if (std::filesystem::exists(besideRose))
                {
                    return besideRose;
                }
            }

            static constexpr const char* candidates[]{
                "tools/stable-diffusion/sd-cli.exe",
                "build-sd/bin/Release/sd-cli.exe",
                "build-sd/bin/Debug/sd-cli.exe",
                "external/stable-diffusion.cpp/build/bin/Release/sd-cli.exe",
                "external/stable-diffusion.cpp/build/bin/Debug/sd-cli.exe",
                "external/stable-diffusion.cpp/bin/Release/sd-cli.exe",
                "sd-cli.exe"
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

            return searchPathExecutable(
                L"sd-cli.exe");
#else
            return {};
#endif
        }


        [[nodiscard]]
        bool usesComponentizedModel(
            const StableDiffusionCliConfig& config) noexcept
        {
            return
                !config.diffusionModelPath.empty()
                || !config.vaePath.empty()
                || !config.llmPath.empty();
        }


        [[nodiscard]]
        std::string validateModelConfiguration(
            const StableDiffusionCliConfig& config)
        {
            const bool componentized =
                usesComponentizedModel(
                    config);

            if (
                componentized
                && !config.modelPath.empty())
            {
                return
                    "Image generator configuration cannot use both --model and "
                    "--diffusion-model component paths at the same time.";
            }

            if (!componentized)
            {
                if (config.modelPath.empty())
                {
                    return "No image-generation model is configured.";
                }

                if (!std::filesystem::exists(
                        config.modelPath))
                {
                    return
                        "Image-generation model not found: "
                        + config.modelPath.string();
                }

                return {};
            }

            if (config.diffusionModelPath.empty())
            {
                return
                    "Componentized image generation requires a diffusion model.";
            }

            if (config.vaePath.empty())
            {
                return
                    "Componentized image generation requires a VAE model.";
            }

            if (config.llmPath.empty())
            {
                return
                    "Componentized image generation requires an LLM/text encoder.";
            }

            if (!std::filesystem::exists(
                    config.diffusionModelPath))
            {
                return
                    "Diffusion model not found: "
                    + config.diffusionModelPath.string();
            }

            if (!std::filesystem::exists(
                    config.vaePath))
            {
                return
                    "Image VAE not found: "
                    + config.vaePath.string();
            }

            if (!std::filesystem::exists(
                    config.llmPath))
            {
                return
                    "Image text encoder not found: "
                    + config.llmPath.string();
            }

            return {};
        }


        void validateRequest(
            const ImageGenerationRequest& request)
        {
            if (request.prompt.empty())
            {
                throw std::invalid_argument{
                    "Image generation requires a non-empty prompt."
                };
            }

            if (
                request.width <= 0
                || request.height <= 0)
            {
                throw std::invalid_argument{
                    "Image dimensions must be greater than zero."
                };
            }

            if (
                request.width > 4096
                || request.height > 4096)
            {
                throw std::invalid_argument{
                    "Rose's MVP image generator limits each dimension to 4096 pixels."
                };
            }

            if (
                request.steps <= 0
                || request.steps > 250)
            {
                throw std::invalid_argument{
                    "Image generation steps must be within 1..250."
                };
            }

            if (
                request.cfgScale < 0.0f
                || request.cfgScale > 50.0f)
            {
                throw std::invalid_argument{
                    "Image generation CFG scale must be within 0..50."
                };
            }
        }
    }


    struct StableDiffusionCliGenerator::Impl
    {
        explicit Impl(
            StableDiffusionCliConfig config)
            : config_{ std::move(config) }
        {
        }


        [[nodiscard]]
        bool available() const noexcept
        {
            try
            {
#ifdef _WIN32
                const auto executable =
                    resolveExecutable(config_);

                return
                    !executable.empty()
                    && std::filesystem::exists(executable)
                    && validateModelConfiguration(
                        config_).empty();
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
            return "Rose's first stable-diffusion.cpp image generator currently supports Windows only.";
#else
            const auto executable =
                resolveExecutable(config_);

            if (executable.empty())
            {
                return "sd-cli.exe was not found beside Rose, in build-sd, external/stable-diffusion.cpp, tools/stable-diffusion, or PATH.";
            }

            if (!std::filesystem::exists(executable))
            {
                return "Configured sd-cli.exe does not exist: "
                    + executable.string();
            }

            const std::string modelConfigurationError =
                validateModelConfiguration(
                    config_);

            if (!modelConfigurationError.empty())
            {
                return modelConfigurationError;
            }

            return "Local stable-diffusion.cpp image generation is available.";
#endif
        }


        [[nodiscard]]
        ImageGenerationResult generate(
            const ImageGenerationRequest& request,
            const std::filesystem::path& destinationPath)
        {
            validateRequest(request);

            if (!available())
            {
                throw std::runtime_error{
                    availabilityMessage()
                };
            }

#ifndef _WIN32
            throw std::runtime_error{
                "Rose's first stable-diffusion.cpp image generator currently supports Windows only."
            };
#else
            const std::filesystem::path executable =
                std::filesystem::absolute(
                    resolveExecutable(config_));

            const bool componentized =
                usesComponentizedModel(
                    config_);

            const std::filesystem::path modelPath =
                config_.modelPath.empty()
                    ? std::filesystem::path{}
                    : std::filesystem::absolute(
                        config_.modelPath);

            const std::filesystem::path diffusionModelPath =
                config_.diffusionModelPath.empty()
                    ? std::filesystem::path{}
                    : std::filesystem::absolute(
                        config_.diffusionModelPath);

            const std::filesystem::path vaePath =
                config_.vaePath.empty()
                    ? std::filesystem::path{}
                    : std::filesystem::absolute(
                        config_.vaePath);

            const std::filesystem::path llmPath =
                config_.llmPath.empty()
                    ? std::filesystem::path{}
                    : std::filesystem::absolute(
                        config_.llmPath);

            const std::filesystem::path outputPath =
                std::filesystem::absolute(
                    destinationPath);

            std::filesystem::create_directories(
                outputPath.parent_path());

            // Keep the backend process log beside the intended image from the
            // moment the child process starts. A failed generation therefore still
            // leaves useful local evidence, while successful generations retain a
            // directly associated diagnostic sidecar.
            const std::filesystem::path logPath =
                outputPath.string()
                + ".sdcli.log.txt";

            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;

            Handle logHandle{
                CreateFileW(
                    logPath.c_str(),
                    GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &security,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr)
            };

            if (
                logHandle.get() == INVALID_HANDLE_VALUE
                || logHandle.get() == nullptr)
            {
                throw std::runtime_error{
                    "Could not create stable-diffusion.cpp process log. Windows error: "
                    + std::to_string(
                        GetLastError())
                };
            }

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
                executable.wstring());

            if (componentized)
            {
                appendArgument(L"--diffusion-model");
                appendArgument(
                    diffusionModelPath.wstring());

                appendArgument(L"--vae");
                appendArgument(
                    vaePath.wstring());

                appendArgument(L"--llm");
                appendArgument(
                    llmPath.wstring());
            }
            else
            {
                appendArgument(L"--model");
                appendArgument(
                    modelPath.wstring());
            }

            appendArgument(L"--prompt");
            appendArgument(
                utf8ToWide(
                    request.prompt));

            if (!request.negativePrompt.empty())
            {
                appendArgument(L"--negative-prompt");
                appendArgument(
                    utf8ToWide(
                        request.negativePrompt));
            }

            appendArgument(L"--width");
            appendArgument(
                std::to_wstring(
                    request.width));

            appendArgument(L"--height");
            appendArgument(
                std::to_wstring(
                    request.height));

            appendArgument(L"--steps");
            appendArgument(
                std::to_wstring(
                    request.steps));

            appendArgument(L"--cfg-scale");
            appendArgument(
                std::to_wstring(
                    request.cfgScale));

            if (request.seed.has_value())
            {
                appendArgument(L"--seed");
                appendArgument(
                    std::to_wstring(
                        *request.seed));
            }

            if (!config_.backendAssignment.empty())
            {
                appendArgument(L"--backend");
                appendArgument(
                    utf8ToWide(
                        config_.backendAssignment));
            }

            if (!config_.paramsBackendAssignment.empty())
            {
                appendArgument(L"--params-backend");
                appendArgument(
                    utf8ToWide(
                        config_.paramsBackendAssignment));
            }

            if (!config_.maxVramAssignment.empty())
            {
                appendArgument(L"--max-vram");
                appendArgument(
                    utf8ToWide(
                        config_.maxVramAssignment));
            }

            appendArgument(L"--auto-fit");
            appendArgument(
                config_.autoFit
                    ? L"on"
                    : L"off");

            if (config_.diffusionFlashAttention)
            {
                appendArgument(
                    L"--diffusion-fa");
            }

            if (config_.offloadToCpu)
            {
                appendArgument(
                    L"--offload-to-cpu");
            }

            appendArgument(L"--output");
            appendArgument(outputPath.wstring());

            appendArgument(L"--log-level");
            appendArgument(L"info");

            for (const std::string& extra :
                config_.extraArguments)
            {
                appendArgument(
                    utf8ToWide(extra));
            }

            std::vector<wchar_t> mutableCommand(
                commandLine.begin(),
                commandLine.end());
            mutableCommand.push_back(L'\0');

            STARTUPINFOW startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            startupInfo.dwFlags = STARTF_USESTDHANDLES;
            startupInfo.hStdOutput = logHandle.get();
            startupInfo.hStdError = logHandle.get();
            startupInfo.hStdInput =
                GetStdHandle(
                    STD_INPUT_HANDLE);

            PROCESS_INFORMATION processInformation{};

            const std::wstring executableString =
                executable.wstring();

            const std::wstring workingDirectory =
                executable.parent_path().wstring();

            const BOOL created =
                CreateProcessW(
                    executableString.c_str(),
                    mutableCommand.data(),
                    nullptr,
                    nullptr,
                    TRUE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    workingDirectory.empty()
                        ? nullptr
                        : workingDirectory.c_str(),
                    &startupInfo,
                    &processInformation);

            if (!created)
            {
                throw std::runtime_error{
                    "Could not start sd-cli.exe. Windows error: "
                    + std::to_string(
                        GetLastError())
                };
            }

            Handle process{
                processInformation.hProcess
            };

            Handle thread{
                processInformation.hThread
            };

            const auto timeoutCount =
                config_.timeout.count();

            const DWORD waitMilliseconds =
                timeoutCount
                    >= static_cast<long long>(INFINITE - 1)
                    ? INFINITE - 1
                    : static_cast<DWORD>(
                        timeoutCount);

            const DWORD waitResult =
                WaitForSingleObject(
                    process.get(),
                    waitMilliseconds);

            if (waitResult == WAIT_TIMEOUT)
            {
                TerminateProcess(
                    process.get(),
                    124);

                WaitForSingleObject(
                    process.get(),
                    5000);

                logHandle.reset();

                throw std::runtime_error{
                    "Local image generation timed out.\nDiagnostic log: "
                    + logPath.string()
                    + "\n"
                    + readTextFile(logPath)
                };
            }

            if (waitResult != WAIT_OBJECT_0)
            {
                throw std::runtime_error{
                    "Waiting for sd-cli.exe failed. Windows error: "
                    + std::to_string(
                        GetLastError())
                };
            }

            DWORD exitCode{ 1 };

            if (!GetExitCodeProcess(
                    process.get(),
                    &exitCode))
            {
                throw std::runtime_error{
                    "Could not read sd-cli.exe exit status."
                };
            }

            // Flush/close our inherited log handle before reading it back.
            logHandle.reset();

            const std::string processLog =
                readTextFile(logPath);

            if (exitCode != 0)
            {
                throw std::runtime_error{
                    "stable-diffusion.cpp failed with exit code "
                    + std::to_string(exitCode)
                    + ".\nDiagnostic log: "
                    + logPath.string()
                    + "\n"
                    + processLog
                };
            }

            if (
                !std::filesystem::exists(outputPath)
                || !std::filesystem::is_regular_file(outputPath)
                || std::filesystem::file_size(outputPath) == 0)
            {
                throw std::runtime_error{
                    "stable-diffusion.cpp completed without producing the expected image.\nDiagnostic log: "
                    + logPath.string()
                    + "\n"
                    + processLog
                };
            }

            ImageGenerationResult result{
                .outputPath = outputPath,
                .providerName = "stable-diffusion.cpp",
                .seed = request.seed,
                .diagnosticLogPath = logPath
            };

            const auto addProvenance =
                [&result](
                    std::string key,
                    std::string value)
                {
                    if (!value.empty())
                    {
                        result.provenance.push_back(
                            ImageGenerationProvenanceEntry{
                                .key = std::move(key),
                                .value = std::move(value)
                            });
                    }
                };

            addProvenance(
                "model_id",
                config_.modelId);
            addProvenance(
                "model_display_name",
                config_.modelDisplayName);
            addProvenance(
                "backend_executable",
                executable.string());
            addProvenance(
                "model_topology",
                componentized
                    ? "componentized"
                    : "single-checkpoint");

            if (componentized)
            {
                addProvenance(
                    "diffusion_model_path",
                    diffusionModelPath.string());
                addProvenance(
                    "vae_path",
                    vaePath.string());
                addProvenance(
                    "text_encoder_path",
                    llmPath.string());
            }
            else
            {
                addProvenance(
                    "model_path",
                    modelPath.string());
            }

            addProvenance(
                "backend_assignment",
                config_.backendAssignment);
            addProvenance(
                "params_backend_assignment",
                config_.paramsBackendAssignment);
            addProvenance(
                "max_vram_assignment",
                config_.maxVramAssignment);
            addProvenance(
                "auto_fit",
                config_.autoFit
                    ? "true"
                    : "false");
            addProvenance(
                "diffusion_flash_attention",
                config_.diffusionFlashAttention
                    ? "true"
                    : "false");
            addProvenance(
                "offload_to_cpu",
                config_.offloadToCpu
                    ? "true"
                    : "false");

            for (std::size_t index = 0;
                 index < config_.extraArguments.size();
                 ++index)
            {
                addProvenance(
                    "extra_argument_"
                        + std::to_string(index),
                    config_.extraArguments[index]);
            }

            return result;
#endif
        }


        StableDiffusionCliConfig config_;
    };


    StableDiffusionCliGenerator::StableDiffusionCliGenerator(
        StableDiffusionCliConfig config)
        : impl_{
            std::make_unique<Impl>(
                std::move(config))
        }
    {
    }


    StableDiffusionCliGenerator::~StableDiffusionCliGenerator() = default;


    bool StableDiffusionCliGenerator::available() const noexcept
    {
        return impl_->available();
    }


    std::string StableDiffusionCliGenerator::availabilityMessage() const
    {
        return impl_->availabilityMessage();
    }


    ImageGenerationResult StableDiffusionCliGenerator::generate(
        const ImageGenerationRequest& request,
        const std::filesystem::path& destinationPath)
    {
        return impl_->generate(
            request,
            destinationPath);
    }

} // namespace rose::imagegen
