#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ocr/TesseractCliOcrEngine.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace rose::ocr
{

    namespace
    {
        class TemporaryFile final
        {
        public:
            explicit TemporaryFile(
                std::filesystem::path path)
                : path_{ std::move(path) }
            {
            }

            ~TemporaryFile()
            {
                std::error_code ignored;
                std::filesystem::remove(
                    path_,
                    ignored);
            }

            TemporaryFile(const TemporaryFile&) = delete;
            TemporaryFile& operator=(const TemporaryFile&) = delete;

            TemporaryFile(TemporaryFile&& other) noexcept
                : path_{ std::move(other.path_) }
            {
                other.path_.clear();
            }

            TemporaryFile& operator=(TemporaryFile&&) = delete;

            [[nodiscard]]
            const std::filesystem::path& path() const noexcept
            {
                return path_;
            }

        private:
            std::filesystem::path path_;
        };


        [[nodiscard]]
        std::string safeImageExtension(
            const std::string_view sourceExtension)
        {
            std::string result;

            for (const char value : sourceExtension)
            {
                const unsigned char character =
                    static_cast<unsigned char>(value);

                if (
                    value == '.'
                    || std::isalnum(character) != 0)
                {
                    result.push_back(
                        static_cast<char>(
                            std::tolower(character)));
                }
            }

            if (result.empty())
            {
                return ".img";
            }

            if (result.front() != '.')
            {
                result.insert(
                    result.begin(),
                    '.');
            }

            // Do not allow a model/user-supplied extension to become an arbitrary
            // long temporary path component.
            constexpr std::size_t maximumExtensionLength{ 12 };

            if (result.size() > maximumExtensionLength)
            {
                return ".img";
            }

            return result;
        }


        void writeBytes(
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
                    "Could not create Rose OCR temporary image: "
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
                    "Could not write Rose OCR temporary image: "
                    + path.string()
                };
            }
        }


        void writeLittleEndian16(
            std::ostream& stream,
            const std::uint16_t value)
        {
            const std::array<char, 2> bytes{
                static_cast<char>(value & 0xFFu),
                static_cast<char>((value >> 8u) & 0xFFu)
            };

            stream.write(
                bytes.data(),
                static_cast<std::streamsize>(bytes.size()));
        }


        void writeLittleEndian32(
            std::ostream& stream,
            const std::uint32_t value)
        {
            const std::array<char, 4> bytes{
                static_cast<char>(value & 0xFFu),
                static_cast<char>((value >> 8u) & 0xFFu),
                static_cast<char>((value >> 16u) & 0xFFu),
                static_cast<char>((value >> 24u) & 0xFFu)
            };

            stream.write(
                bytes.data(),
                static_cast<std::streamsize>(bytes.size()));
        }


        void writeBitmapAsBmp(
            const std::filesystem::path& path,
            const OcrBitmap& bitmap)
        {
            if (
                bitmap.width <= 0
                || bitmap.height <= 0)
            {
                throw std::invalid_argument{
                    "OCR bitmap dimensions must be positive."
                };
            }

            const std::size_t minimumStride =
                static_cast<std::size_t>(bitmap.width)
                * 4u;

            if (
                bitmap.strideBytes < minimumStride
                || bitmap.bgra.size()
                    < bitmap.strideBytes
                        * static_cast<std::size_t>(bitmap.height))
            {
                throw std::invalid_argument{
                    "OCR bitmap storage is smaller than its declared dimensions."
                };
            }

            const std::uint64_t pixelBytes64 =
                static_cast<std::uint64_t>(minimumStride)
                * static_cast<std::uint64_t>(bitmap.height);

            constexpr std::uint32_t fileHeaderSize{ 14 };
            constexpr std::uint32_t infoHeaderSize{ 40 };
            constexpr std::uint32_t pixelOffset{
                fileHeaderSize + infoHeaderSize
            };

            if (
                pixelBytes64
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max()
                    - pixelOffset))
            {
                throw std::runtime_error{
                    "OCR bitmap is too large to serialize as BMP."
                };
            }

            const std::uint32_t pixelBytes =
                static_cast<std::uint32_t>(pixelBytes64);

            const std::uint32_t fileSize =
                pixelOffset + pixelBytes;

            std::ofstream stream{
                path,
                std::ios::binary | std::ios::trunc
            };

            if (!stream)
            {
                throw std::runtime_error{
                    "Could not create OCR BMP: "
                    + path.string()
                };
            }

            // BITMAPFILEHEADER (14 bytes)
            stream.put('B');
            stream.put('M');
            writeLittleEndian32(stream, fileSize);
            writeLittleEndian16(stream, 0);
            writeLittleEndian16(stream, 0);
            writeLittleEndian32(stream, pixelOffset);

            // BITMAPINFOHEADER (40 bytes)
            writeLittleEndian32(stream, infoHeaderSize);
            writeLittleEndian32(
                stream,
                static_cast<std::uint32_t>(bitmap.width));
            writeLittleEndian32(
                stream,
                static_cast<std::uint32_t>(bitmap.height));
            writeLittleEndian16(stream, 1);     // planes
            writeLittleEndian16(stream, 32);    // BGRA/BGRx
            writeLittleEndian32(stream, 0);     // BI_RGB
            writeLittleEndian32(stream, pixelBytes);
            writeLittleEndian32(stream, 0);     // x pixels/meter
            writeLittleEndian32(stream, 0);     // y pixels/meter
            writeLittleEndian32(stream, 0);     // colors used
            writeLittleEndian32(stream, 0);     // important colors

            // BMP with positive height stores bottom row first. Rose's OcrBitmap is
            // top-down, so reverse rows while serializing. 32-bit rows are naturally
            // 4-byte aligned and need no extra padding.
            for (
                int y = bitmap.height - 1;
                y >= 0;
                --y)
            {
                const std::uint8_t* row =
                    bitmap.bgra.data()
                    + static_cast<std::size_t>(y)
                        * bitmap.strideBytes;

                stream.write(
                    reinterpret_cast<const char*>(row),
                    static_cast<std::streamsize>(minimumStride));
            }

            if (!stream)
            {
                throw std::runtime_error{
                    "Could not finish writing OCR BMP: "
                    + path.string()
                };
            }
        }


#ifdef _WIN32
        struct BoundedTextFile
        {
            std::string text;
            bool truncated{ false };
        };


        [[nodiscard]]
        BoundedTextFile readTextFileBounded(
            const std::filesystem::path& path,
            const std::size_t maximumBytes)
        {
            std::ifstream stream{
                path,
                std::ios::binary
            };

            if (!stream)
            {
                return {};
            }

            // Read one extra byte so truncation is observable instead of silent.
            const std::size_t requested =
                maximumBytes
                < std::numeric_limits<std::size_t>::max()
                    ? maximumBytes + 1u
                    : maximumBytes;

            std::string text;
            text.resize(requested);

            stream.read(
                text.data(),
                static_cast<std::streamsize>(text.size()));

            const std::streamsize actual =
                stream.gcount();

            if (actual < 0)
            {
                return {};
            }

            text.resize(
                static_cast<std::size_t>(actual));

            const bool truncated =
                text.size() > maximumBytes;

            if (truncated)
            {
                text.resize(maximumBytes);

                // Tesseract emits UTF-8. Avoid ending the transient context in the
                // middle of a multi-byte code point.
                while (
                    !text.empty()
                    && (
                        static_cast<unsigned char>(text.back())
                        & 0xC0u)
                    == 0x80u)
                {
                    text.pop_back();
                }
            }

            if (
                text.size() >= 3
                && static_cast<unsigned char>(text[0]) == 0xEFu
                && static_cast<unsigned char>(text[1]) == 0xBBu
                && static_cast<unsigned char>(text[2]) == 0xBFu)
            {
                text.erase(0, 3);
            }

            return BoundedTextFile{
                .text = std::move(text),
                .truncated = truncated
            };
        }
#endif


#ifdef _WIN32
        [[nodiscard]]
        std::wstring quoteWindowsArgument(
            const std::wstring_view value)
        {
            // Always quoting simplifies the rules and safely handles spaces,
            // backslashes, and embedded quotes according to CommandLineToArgvW's
            // conventional parsing rules.
            std::wstring result;
            result.push_back(L'"');

            std::size_t backslashes{ 0 };

            for (const wchar_t character : value)
            {
                if (character == L'\\')
                {
                    ++backslashes;
                    continue;
                }

                if (character == L'"')
                {
                    result.append(
                        backslashes * 2u + 1u,
                        L'\\');
                    result.push_back(L'"');
                    backslashes = 0;
                    continue;
                }

                result.append(
                    backslashes,
                    L'\\');
                backslashes = 0;
                result.push_back(character);
            }

            // Backslashes immediately before the closing quote must be doubled.
            result.append(
                backslashes * 2u,
                L'\\');
            result.push_back(L'"');

            return result;
        }


        [[nodiscard]]
        std::filesystem::path searchPathForTesseract()
        {
            std::array<wchar_t, 32768> buffer{};

            const DWORD length =
                SearchPathW(
                    nullptr,
                    L"tesseract.exe",
                    nullptr,
                    static_cast<DWORD>(buffer.size()),
                    buffer.data(),
                    nullptr);

            if (
                length == 0
                || length >= buffer.size())
            {
                return {};
            }

            return std::filesystem::path{
                std::wstring_view{
                    buffer.data(),
                    length
                }
            };
        }
#endif


        [[nodiscard]]
        std::filesystem::path resolveTesseractExecutable(
            const std::filesystem::path& configured)
        {
            std::error_code error;

            if (!configured.empty())
            {
                if (
                    std::filesystem::is_regular_file(
                        configured,
                        error)
                    && !error)
                {
                    return std::filesystem::absolute(
                        configured);
                }

                return {};
            }

            const std::array<std::filesystem::path, 5> candidates{
                std::filesystem::path{
                    "tools/tesseract/tesseract.exe"
                },
                std::filesystem::path{
                    "external/tesseract/tesseract.exe"
                },
                std::filesystem::path{
                    "C:/Program Files/Tesseract-OCR/tesseract.exe"
                },
                std::filesystem::path{
                    "C:/Program Files (x86)/Tesseract-OCR/tesseract.exe"
                },
                std::filesystem::path{
                    "D:/Program Files/Tesseract-OCR/tesseract.exe"
                }
            };

            for (const auto& candidate : candidates)
            {
                error.clear();

                if (
                    std::filesystem::is_regular_file(
                        candidate,
                        error)
                    && !error)
                {
                    return std::filesystem::absolute(
                        candidate);
                }
            }

#ifdef _WIN32
            return searchPathForTesseract();
#else
            return {};
#endif
        }


        [[nodiscard]]
        std::filesystem::path resolveTessdataDirectory(
            const std::filesystem::path& configured,
            const std::filesystem::path& executable)
        {
            std::error_code error;

            if (!configured.empty())
            {
                if (
                    std::filesystem::is_directory(
                        configured,
                        error)
                    && !error)
                {
                    return std::filesystem::absolute(
                        configured);
                }

                return {};
            }

            if (!executable.empty())
            {
                const std::filesystem::path sibling =
                    executable.parent_path()
                    / "tessdata";

                if (
                    std::filesystem::is_directory(
                        sibling,
                        error)
                    && !error)
                {
                    return sibling;
                }
            }

            return {};
        }

    } // namespace


    TesseractCliOcrEngine::TesseractCliOcrEngine(
        TesseractCliOcrConfig config)
        : config_{ std::move(config) }
        , executablePath_{
            resolveTesseractExecutable(
                config_.executablePath)
        }
        , tessdataDirectory_{
            resolveTessdataDirectory(
                config_.tessdataDirectory,
                executablePath_)
        }
    {
        if (config_.language.empty())
        {
            unavailableReason_ =
                "Rose OCR has no configured Tesseract language.";
            return;
        }

        if (config_.maximumOutputUtf8Bytes == 0)
        {
            unavailableReason_ =
                "Rose OCR output limit is configured as zero bytes.";
            return;
        }

        if (config_.timeout.count() <= 0)
        {
            unavailableReason_ =
                "Rose OCR timeout must be greater than zero.";
            return;
        }

#ifndef _WIN32
        unavailableReason_ =
            "The current Tesseract CLI adapter is implemented for Windows only.";
#else
        if (executablePath_.empty())
        {
            unavailableReason_ =
                "Tesseract OCR is not installed or bundled. Rose looked in "
                "tools/tesseract, external/tesseract, Program Files, and PATH.";
            return;
        }
#endif
    }


    bool TesseractCliOcrEngine::available() const noexcept
    {
        return unavailableReason_.empty();
    }


    std::string TesseractCliOcrEngine::availabilityMessage() const
    {
        if (available())
        {
            return
                "Tesseract OCR available at: "
                + executablePath_.string();
        }

        return unavailableReason_;
    }


    std::filesystem::path
        TesseractCliOcrEngine::makeTemporaryStem() const
    {
        static std::atomic<std::uint64_t> sequence{ 0 };

        std::filesystem::path directory =
            std::filesystem::temp_directory_path()
            / "Rose"
            / "ocr";

        std::filesystem::create_directories(
            directory);

        const std::uint64_t id =
            sequence.fetch_add(
                1,
                std::memory_order_relaxed);

#ifdef _WIN32
        const auto processId =
            static_cast<std::uint64_t>(
                GetCurrentProcessId());

        const auto tick =
            static_cast<std::uint64_t>(
                GetTickCount64());
#else
        const std::uint64_t processId{ 0 };
        const std::uint64_t tick{ 0 };
#endif

        return directory
            / (
                "rose_ocr_"
                + std::to_string(processId)
                + "_"
                + std::to_string(tick)
                + "_"
                + std::to_string(id));
    }


    OcrResult TesseractCliOcrEngine::recognizeEncodedImage(
        const std::span<const std::uint8_t> bytes,
        const std::string_view sourceExtension)
    {
        if (!available())
        {
            throw std::runtime_error{
                availabilityMessage()
            };
        }

        if (bytes.empty())
        {
            throw std::invalid_argument{
                "Cannot OCR an empty image attachment."
            };
        }

        const std::filesystem::path stem =
            makeTemporaryStem();

        TemporaryFile image{
            stem.string()
            + safeImageExtension(sourceExtension)
        };

        writeBytes(
            image.path(),
            bytes);

        return recognizeTemporaryImage(
            image.path());
    }


    OcrResult TesseractCliOcrEngine::recognizeBitmap(
        const OcrBitmap& bitmap)
    {
        if (!available())
        {
            throw std::runtime_error{
                availabilityMessage()
            };
        }

        const std::filesystem::path stem =
            makeTemporaryStem();

        TemporaryFile image{
            stem.string()
            + ".bmp"
        };

        writeBitmapAsBmp(
            image.path(),
            bitmap);

        return recognizeTemporaryImage(
            image.path());
    }


    OcrResult TesseractCliOcrEngine::recognizeTemporaryImage(
        const std::filesystem::path& imagePath)
    {
#ifndef _WIN32
        (void)imagePath;

        throw std::runtime_error{
            "The current Tesseract CLI OCR adapter is Windows-only."
        };
#else
        const std::filesystem::path outputStem =
            makeTemporaryStem();

        TemporaryFile outputText{
            outputStem.string()
            + ".txt"
        };

        // Tesseract itself adds .txt, so remove our placeholder before launch.
        {
            std::error_code ignored;
            std::filesystem::remove(
                outputText.path(),
                ignored);
        }

        TemporaryFile diagnostic{
            outputStem.string()
            + ".log"
        };


        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength =
            sizeof(securityAttributes);
        securityAttributes.bInheritHandle =
            TRUE;


        HANDLE diagnosticHandle =
            CreateFileW(
                diagnostic.path().c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ,
                &securityAttributes,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_TEMPORARY,
                nullptr);

        if (diagnosticHandle == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error{
                "Could not create Tesseract diagnostic output file."
            };
        }


        struct HandleCloser
        {
            HANDLE handle{ nullptr };

            ~HandleCloser()
            {
                if (
                    handle != nullptr
                    && handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle);
                }
            }
        } diagnosticGuard{
            diagnosticHandle
        };


        std::wstring command =
            quoteWindowsArgument(
                executablePath_.wstring());

        command += L" ";
        command += quoteWindowsArgument(
            imagePath.wstring());

        command += L" ";
        command += quoteWindowsArgument(
            outputStem.wstring());

        command += L" -l ";
        command += quoteWindowsArgument(
            std::wstring{
                config_.language.begin(),
                config_.language.end()
            });

        command += L" --psm ";
        command += std::to_wstring(
            config_.pageSegmentationMode);

        if (!tessdataDirectory_.empty())
        {
            command += L" --tessdata-dir ";
            command += quoteWindowsArgument(
                tessdataDirectory_.wstring());
        }


        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput =
            GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput =
            diagnosticHandle;
        startup.hStdError =
            diagnosticHandle;

        PROCESS_INFORMATION process{};

        std::vector<wchar_t> mutableCommand(
            command.begin(),
            command.end());
        mutableCommand.push_back(L'\0');


        const BOOL created =
            CreateProcessW(
                executablePath_.c_str(),
                mutableCommand.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startup,
                &process);

        if (!created)
        {
            throw std::runtime_error{
                "Could not start Tesseract OCR process. Windows error: "
                + std::to_string(
                    GetLastError())
            };
        }


        HandleCloser processGuard{
            process.hProcess
        };
        HandleCloser threadGuard{
            process.hThread
        };


        const DWORD waitMilliseconds =
            config_.timeout
                > std::chrono::milliseconds{
                    std::numeric_limits<DWORD>::max()
                }
                ? INFINITE
                : static_cast<DWORD>(
                    config_.timeout.count());

        const DWORD waitResult =
            WaitForSingleObject(
                process.hProcess,
                waitMilliseconds);

        if (waitResult == WAIT_TIMEOUT)
        {
            TerminateProcess(
                process.hProcess,
                1);

            throw std::runtime_error{
                "Tesseract OCR exceeded Rose's configured timeout."
            };
        }

        if (waitResult != WAIT_OBJECT_0)
        {
            throw std::runtime_error{
                "Waiting for Tesseract OCR failed. Windows error: "
                + std::to_string(
                    GetLastError())
            };
        }


        DWORD exitCode{ 0 };

        if (!GetExitCodeProcess(
            process.hProcess,
            &exitCode))
        {
            throw std::runtime_error{
                "Could not read Tesseract OCR exit status."
            };
        }

        // Flush/close the inherited diagnostic handle before reading the file.
        diagnosticGuard.handle = nullptr;
        CloseHandle(diagnosticHandle);


        if (exitCode != 0)
        {
            const BoundedTextFile details =
                readTextFileBounded(
                    diagnostic.path(),
                    8192u);

            throw std::runtime_error{
                "Tesseract OCR failed with exit code "
                + std::to_string(exitCode)
                + (
                    details.text.empty()
                        ? std::string{}
                        : std::string{ ": " } + details.text)
            };
        }


        BoundedTextFile output =
            readTextFileBounded(
                outputText.path(),
                config_.maximumOutputUtf8Bytes);

        return OcrResult{
            .text = std::move(output.text),
            .truncated = output.truncated
        };
#endif
    }

} // namespace rose::ocr
