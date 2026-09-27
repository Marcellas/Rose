#include "media/MediaService.h"

#include "integrations/SimpleJson.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objidl.h>
#include <propidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace rose::media
{
    namespace
    {
        class TemporaryDirectory final
        {
        public:
            TemporaryDirectory()
            {
                static std::atomic<std::uint64_t> sequence{ 0 };
                const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
                path_ = std::filesystem::temp_directory_path()
                    / ("Rose-media-" + std::to_string(now) + "-"
                       + std::to_string(sequence.fetch_add(1)));
                std::error_code error;
                if (!std::filesystem::create_directories(path_, error) || error)
                {
                    throw std::runtime_error{ "Could not create Rose media temporary directory." };
                }
            }

            ~TemporaryDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path_, ignored);
            }

            [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

        private:
            std::filesystem::path path_;
        };

        [[nodiscard, maybe_unused]] std::string readTextFile(const std::filesystem::path& path)
        {
            std::ifstream input{ path, std::ios::binary };
            if (!input) throw std::runtime_error{ "Could not read FFprobe output." };
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        [[nodiscard, maybe_unused]] std::vector<std::uint8_t> readBinaryFileBounded(
            const std::filesystem::path& path,
            const std::size_t maximumBytes)
        {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error || size == 0 || size > maximumBytes
                || size > (std::numeric_limits<std::size_t>::max)())
            {
                throw std::runtime_error{ "FFmpeg produced an invalid or oversized representative frame." };
            }
            std::ifstream input{ path, std::ios::binary };
            if (!input) throw std::runtime_error{ "Could not read FFmpeg representative frame." };
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
            {
                throw std::runtime_error{ "Could not read the complete FFmpeg representative frame." };
            }
            return bytes;
        }

        [[nodiscard, maybe_unused]] double numberOrStringDouble(
            const integrations::json::Value& object,
            const std::string_view key,
            const double fallback = 0.0)
        {
            const auto* value = object.find(key);
            if (!value) return fallback;
            if (const auto number = value->number()) return *number;
            if (const auto* text = value->string())
            {
                try
                {
                    std::size_t used{ 0 };
                    const double parsed = std::stod(*text, &used);
                    if (used == text->size() && std::isfinite(parsed)) return parsed;
                }
                catch (...) {}
            }
            return fallback;
        }

        [[nodiscard, maybe_unused]] std::uintmax_t integerOrStringUint(
            const integrations::json::Value& object,
            const std::string_view key,
            const std::uintmax_t fallback = 0)
        {
            const auto* value = object.find(key);
            if (!value) return fallback;
            if (const auto number = value->number())
            {
                if (*number >= 0.0 && *number <= static_cast<double>((std::numeric_limits<std::uintmax_t>::max)()))
                    return static_cast<std::uintmax_t>(*number);
            }
            if (const auto* text = value->string())
            {
                try { return static_cast<std::uintmax_t>(std::stoull(*text)); }
                catch (...) {}
            }
            return fallback;
        }

#ifdef _WIN32
        [[nodiscard]] std::wstring quoteWindowsArgument(const std::wstring& value)
        {
            if (value.find_first_of(L" \t\n\v\"") == std::wstring::npos) return value;
            std::wstring result{ L'\"' };
            std::size_t slashes{ 0 };
            for (const wchar_t character : value)
            {
                if (character == L'\\') { ++slashes; continue; }
                if (character == L'\"')
                {
                    result.append(slashes * 2u + 1u, L'\\');
                    result.push_back(L'\"');
                    slashes = 0;
                    continue;
                }
                result.append(slashes, L'\\');
                slashes = 0;
                result.push_back(character);
            }
            result.append(slashes * 2u, L'\\');
            result.push_back(L'\"');
            return result;
        }

        [[nodiscard]] std::filesystem::path searchPathExecutable(const wchar_t* name)
        {
            const DWORD required = SearchPathW(nullptr, name, nullptr, 0, nullptr, nullptr);
            if (required == 0) return {};
            std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1u);
            const DWORD written = SearchPathW(nullptr, name, nullptr,
                static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
            if (written == 0 || written >= buffer.size()) return {};
            return std::filesystem::path{ std::wstring{ buffer.data(), written } };
        }

        [[nodiscard]] std::filesystem::path resolveExecutable(
            const std::filesystem::path& configured,
            const wchar_t* executableName)
        {
            std::error_code error;
            if (!configured.empty())
            {
                if (std::filesystem::is_regular_file(configured, error) && !error)
                    return std::filesystem::absolute(configured);
                return {};
            }

            const std::wstring name{ executableName };
            const std::array<std::filesystem::path, 5> roots{
                "tools/ffmpeg/bin",
                "external/ffmpeg/bin",
                "C:/Program Files/ffmpeg/bin",
                "C:/ffmpeg/bin",
                "D:/Program Files/ffmpeg/bin"
            };
            for (const auto& root : roots)
            {
                const auto candidate = root / name;
                error.clear();
                if (std::filesystem::is_regular_file(candidate, error) && !error)
                    return std::filesystem::absolute(candidate);
            }
            return searchPathExecutable(executableName);
        }

        void runProcess(
            const std::filesystem::path& executable,
            const std::vector<std::wstring>& arguments,
            const std::uint32_t timeoutMilliseconds)
        {
            std::wstring command = quoteWindowsArgument(executable.wstring());
            for (const auto& argument : arguments)
            {
                command.push_back(L' ');
                command += quoteWindowsArgument(argument);
            }

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            std::vector<wchar_t> mutableCommand(command.begin(), command.end());
            mutableCommand.push_back(L'\0');

            if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                    CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            {
                throw std::runtime_error{ "Could not launch FFmpeg/FFprobe." };
            }
            CloseHandle(process.hThread);
            const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMilliseconds);
            DWORD exitCode{ 1 };
            if (wait == WAIT_OBJECT_0) (void)GetExitCodeProcess(process.hProcess, &exitCode);
            else (void)TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hProcess);
            if (wait != WAIT_OBJECT_0) throw std::runtime_error{ "FFmpeg/FFprobe timed out." };
            if (exitCode != 0) throw std::runtime_error{ "FFmpeg/FFprobe could not decode this media file." };
        }
#endif

        void validateMediaPath(
            const std::filesystem::path& path,
            const std::uintmax_t maximumBytes)
        {
            if (!path.is_absolute()) throw std::invalid_argument{ "Media inspection requires an absolute file path." };
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error)
                throw std::runtime_error{ "Media file does not exist or is not readable: " + path.string() };
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error) throw std::runtime_error{ "Could not determine media file size." };
            if (size > maximumBytes) throw std::runtime_error{ "Media file exceeds Rose's configured inspection size limit." };
        }


#ifdef _WIN32
        using Microsoft::WRL::ComPtr;

        [[noreturn]] void throwWindowsMediaError(
            const std::string_view action,
            const HRESULT result)
        {
            std::ostringstream message;
            message << action << " failed (HRESULT 0x"
                    << std::hex << std::uppercase
                    << static_cast<unsigned long>(result) << ").";
            throw std::runtime_error{ message.str() };
        }

        void requireWindowsMediaSuccess(
            const HRESULT result,
            const std::string_view action)
        {
            if (FAILED(result)) throwWindowsMediaError(action, result);
        }

        class WindowsMediaFoundationSession final
        {
        public:
            WindowsMediaFoundationSession()
            {
                const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                if (SUCCEEDED(comResult))
                {
                    uninitializeCom_ = true;
                }
                else if (comResult != RPC_E_CHANGED_MODE)
                {
                    throwWindowsMediaError("CoInitializeEx for native media inspection", comResult);
                }

                const HRESULT mfResult = MFStartup(MF_VERSION, MFSTARTUP_FULL);
                if (FAILED(mfResult))
                {
                    if (uninitializeCom_) CoUninitialize();
                    uninitializeCom_ = false;
                    throwWindowsMediaError("MFStartup for native media inspection", mfResult);
                }
                shutdownMediaFoundation_ = true;
            }

            ~WindowsMediaFoundationSession()
            {
                if (shutdownMediaFoundation_) (void)MFShutdown();
                if (uninitializeCom_) CoUninitialize();
            }

            WindowsMediaFoundationSession(const WindowsMediaFoundationSession&) = delete;
            WindowsMediaFoundationSession& operator=(const WindowsMediaFoundationSession&) = delete;

        private:
            bool uninitializeCom_{ false };
            bool shutdownMediaFoundation_{ false };
        };

        [[nodiscard]] std::string guidText(const GUID& value)
        {
            wchar_t buffer[64]{};
            if (StringFromGUID2(value, buffer, static_cast<int>(std::size(buffer))) <= 0)
                return "unknown";

            // StringFromGUID2 emits the canonical GUID spelling, which is ASCII.
            // Narrow one character at a time so MSVC does not perform an implicit
            // wchar_t -> char conversion through std::string's range constructor.
            std::string text;
            for (const wchar_t character : std::wstring_view{ buffer })
            {
                if (character > 0x7f) return "unknown";
                text.push_back(static_cast<char>(character));
            }
            return text;
        }

        [[nodiscard]] std::string mediaSubtypeName(const GUID& subtype)
        {
            // Most Media Foundation video subtypes use a FOURCC in Data1. Decode
            // it when printable so diagnostics remain useful without tying Rose to
            // an ever-growing table of SDK-specific codec GUID constants.
            std::array<char, 5> fourcc{
                static_cast<char>(subtype.Data1 & 0xFFu),
                static_cast<char>((subtype.Data1 >> 8u) & 0xFFu),
                static_cast<char>((subtype.Data1 >> 16u) & 0xFFu),
                static_cast<char>((subtype.Data1 >> 24u) & 0xFFu),
                '\0'
            };
            const bool printable = std::all_of(
                fourcc.begin(), fourcc.begin() + 4,
                [](const char c)
                {
                    const unsigned char value = static_cast<unsigned char>(c);
                    return value >= 32u && value <= 126u;
                });
            if (printable)
            {
                std::string name{ fourcc.data(), 4u };
                if (name == "VP90") return "vp9";
                if (name == "VP80") return "vp8";
                if (name == "AV01") return "av1";
                if (name == "H264" || name == "h264") return "h264";
                if (name == "HEVC" || name == "H265") return "hevc";
                return name;
            }
            return guidText(subtype);
        }

        [[nodiscard]] std::string nativeFormatName(const std::filesystem::path& path)
        {
            std::string extension = path.extension().string();
            if (!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
            if (extension.empty()) extension = "media";
            return extension + " (Windows Media Foundation)";
        }

        struct NativeDecodedFrame
        {
            std::uint32_t width{ 0 };
            std::uint32_t height{ 0 };
            std::vector<std::uint8_t> bgra;
        };

        [[nodiscard]] std::uint64_t duration100ns(IMFSourceReader& reader)
        {
            PROPVARIANT value;
            PropVariantInit(&value);
            const HRESULT result = reader.GetPresentationAttribute(
                static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
                MF_PD_DURATION,
                &value);
            std::uint64_t duration{ 0 };
            if (SUCCEEDED(result))
            {
                if (value.vt == VT_UI8) duration = value.uhVal.QuadPart;
                else if (value.vt == VT_I8 && value.hVal.QuadPart > 0)
                    duration = static_cast<std::uint64_t>(value.hVal.QuadPart);
            }
            PropVariantClear(&value);
            return duration;
        }

        [[nodiscard]] ComPtr<IMFSourceReader> createNativeSourceReader(
            const std::filesystem::path& path)
        {
            ComPtr<IMFAttributes> attributes;
            requireWindowsMediaSuccess(
                MFCreateAttributes(attributes.GetAddressOf(), 2u),
                "MFCreateAttributes");
            requireWindowsMediaSuccess(
                attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE),
                "Enable Media Foundation video processing");

            ComPtr<IMFSourceReader> reader;
            requireWindowsMediaSuccess(
                MFCreateSourceReaderFromURL(
                    path.c_str(),
                    attributes.Get(),
                    reader.GetAddressOf()),
                "Open media through Windows Media Foundation");
            return reader;
        }

        [[nodiscard]] MediaMetadata probeNativeReader(
            IMFSourceReader& reader,
            const std::filesystem::path& path)
        {
            MediaMetadata metadata;
            metadata.path = path;
            metadata.formatName = nativeFormatName(path);
            metadata.durationSeconds = static_cast<double>(duration100ns(reader)) / 10'000'000.0;

            std::error_code sizeError;
            metadata.sourceBytes = std::filesystem::file_size(path, sizeError);
            if (sizeError) metadata.sourceBytes = 0;

            ComPtr<IMFMediaType> videoType;
            const HRESULT videoResult = reader.GetNativeMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                0,
                videoType.GetAddressOf());
            if (FAILED(videoResult))
                throwWindowsMediaError("Find a native video stream", videoResult);

            GUID videoSubtype{};
            if (SUCCEEDED(videoType->GetGUID(MF_MT_SUBTYPE, &videoSubtype)))
                metadata.videoCodec = mediaSubtypeName(videoSubtype);

            UINT32 width{ 0 };
            UINT32 height{ 0 };
            if (SUCCEEDED(MFGetAttributeSize(videoType.Get(), MF_MT_FRAME_SIZE, &width, &height)))
            {
                metadata.width = static_cast<int>(width);
                metadata.height = static_cast<int>(height);
            }

            UINT32 frameNumerator{ 0 };
            UINT32 frameDenominator{ 0 };
            if (SUCCEEDED(MFGetAttributeRatio(
                    videoType.Get(), MF_MT_FRAME_RATE,
                    &frameNumerator, &frameDenominator))
                && frameDenominator != 0)
            {
                metadata.frameRate = std::to_string(frameNumerator)
                    + "/" + std::to_string(frameDenominator);
            }

            ComPtr<IMFMediaType> audioType;
            if (SUCCEEDED(reader.GetNativeMediaType(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
                    0,
                    audioType.GetAddressOf())))
            {
                metadata.hasAudio = true;
                GUID audioSubtype{};
                if (SUCCEEDED(audioType->GetGUID(MF_MT_SUBTYPE, &audioSubtype)))
                    metadata.audioCodec = mediaSubtypeName(audioSubtype);
            }

            if (metadata.videoCodec.empty()) metadata.videoCodec = "native-video";
            return metadata;
        }

        [[nodiscard]] LONG currentRgbStride(
            IMFSourceReader& reader,
            const std::uint32_t width)
        {
            ComPtr<IMFMediaType> currentType;
            if (SUCCEEDED(reader.GetCurrentMediaType(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                    currentType.GetAddressOf())))
            {
                UINT32 rawStride{ 0 };
                if (SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride)))
                    return static_cast<LONG>(rawStride);
            }
            if (width > static_cast<std::uint32_t>((std::numeric_limits<LONG>::max)() / 4))
                throw std::runtime_error{ "Native media frame width is too large." };
            return static_cast<LONG>(width * 4u);
        }

        [[nodiscard]] NativeDecodedFrame downscaleRgb32(
            const BYTE* bytes,
            const DWORD byteCount,
            const LONG stride,
            const std::uint32_t width,
            const std::uint32_t height)
        {
            if (bytes == nullptr || width == 0 || height == 0)
                throw std::runtime_error{ "Windows Media Foundation returned an empty video frame." };
            if (width > 16'384u || height > 16'384u)
                throw std::runtime_error{ "Native media frame dimensions exceed Rose's safety limit." };

            const std::uint64_t rowBytes = static_cast<std::uint64_t>(std::abs(stride));
            const std::uint64_t required = rowBytes * static_cast<std::uint64_t>(height);
            if (required > byteCount || rowBytes < static_cast<std::uint64_t>(width) * 4u)
                throw std::runtime_error{ "Windows Media Foundation returned an unexpected RGB32 frame layout." };

            constexpr std::uint32_t maximumWidth = 512u;
            const std::uint32_t targetWidth = std::min(width, maximumWidth);
            const std::uint32_t targetHeight = std::max<std::uint32_t>(
                1u,
                static_cast<std::uint32_t>(std::llround(
                    static_cast<double>(height)
                    * static_cast<double>(targetWidth)
                    / static_cast<double>(width))));

            NativeDecodedFrame frame;
            frame.width = targetWidth;
            frame.height = targetHeight;
            frame.bgra.resize(
                static_cast<std::size_t>(targetWidth)
                * static_cast<std::size_t>(targetHeight) * 4u);

            const std::size_t sourceStride = static_cast<std::size_t>(std::abs(stride));
            for (std::uint32_t y = 0; y < targetHeight; ++y)
            {
                const std::uint32_t sourceY = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(y) * height / targetHeight);
                const BYTE* sourceRow = stride >= 0
                    ? bytes + static_cast<std::size_t>(sourceY) * sourceStride
                    : bytes + static_cast<std::size_t>(height - 1u - sourceY) * sourceStride;

                for (std::uint32_t x = 0; x < targetWidth; ++x)
                {
                    const std::uint32_t sourceX = static_cast<std::uint32_t>(
                        static_cast<std::uint64_t>(x) * width / targetWidth);
                    const BYTE* sourcePixel = sourceRow + static_cast<std::size_t>(sourceX) * 4u;
                    std::uint8_t* destination = frame.bgra.data()
                        + (static_cast<std::size_t>(y) * targetWidth + x) * 4u;
                    destination[0] = sourcePixel[0];
                    destination[1] = sourcePixel[1];
                    destination[2] = sourcePixel[2];
                    destination[3] = 255u;
                }
            }
            return frame;
        }

        [[nodiscard]] NativeDecodedFrame readNativeFrame(
            IMFSourceReader& reader,
            const std::uint64_t timestamp100ns,
            const std::uint32_t width,
            const std::uint32_t height)
        {
            PROPVARIANT position;
            PropVariantInit(&position);
            position.vt = VT_I8;
            position.hVal.QuadPart = static_cast<LONGLONG>(timestamp100ns);
            const HRESULT seekResult = reader.SetCurrentPosition(GUID_NULL, position);
            PropVariantClear(&position);
            requireWindowsMediaSuccess(seekResult, "Seek native media source");

            for (int attempt = 0; attempt < 24; ++attempt)
            {
                DWORD actualStream{ 0 };
                DWORD flags{ 0 };
                LONGLONG actualTimestamp{ 0 };
                ComPtr<IMFSample> sample;
                requireWindowsMediaSuccess(
                    reader.ReadSample(
                        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                        0,
                        &actualStream,
                        &flags,
                        &actualTimestamp,
                        sample.GetAddressOf()),
                    "Read native media frame");
                (void)actualStream;
                (void)actualTimestamp;

                if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
                    break;
                if (!sample) continue;

                ComPtr<IMFMediaBuffer> buffer;
                requireWindowsMediaSuccess(
                    sample->ConvertToContiguousBuffer(buffer.GetAddressOf()),
                    "Flatten native media frame");

                BYTE* data{ nullptr };
                DWORD maximumLength{ 0 };
                DWORD currentLength{ 0 };
                requireWindowsMediaSuccess(
                    buffer->Lock(&data, &maximumLength, &currentLength),
                    "Lock native media frame");
                (void)maximumLength;
                try
                {
                    const LONG stride = currentRgbStride(reader, width);
                    NativeDecodedFrame frame = downscaleRgb32(
                        data, currentLength, stride, width, height);
                    (void)buffer->Unlock();
                    return frame;
                }
                catch (...)
                {
                    (void)buffer->Unlock();
                    throw;
                }
            }
            throw std::runtime_error{ "Windows Media Foundation could not produce a representative video frame." };
        }

        [[nodiscard]] std::vector<std::uint8_t> encodeBgraPng(
            const std::vector<std::uint8_t>& pixels,
            const std::uint32_t width,
            const std::uint32_t height,
            const std::size_t maximumBytes)
        {
            if (pixels.empty() || width == 0 || height == 0)
                throw std::runtime_error{ "Cannot encode an empty native media contact sheet." };

            ComPtr<IWICImagingFactory> factory;
            requireWindowsMediaSuccess(
                CoCreateInstance(
                    CLSID_WICImagingFactory,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(factory.GetAddressOf())),
                "Create Windows Imaging Component factory");

            ComPtr<IStream> stream;
            requireWindowsMediaSuccess(
                CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()),
                "Create native media PNG memory stream");

            ComPtr<IWICBitmapEncoder> encoder;
            requireWindowsMediaSuccess(
                factory->CreateEncoder(
                    GUID_ContainerFormatPng,
                    nullptr,
                    encoder.GetAddressOf()),
                "Create native media PNG encoder");
            requireWindowsMediaSuccess(
                encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache),
                "Initialize native media PNG encoder");

            ComPtr<IWICBitmapFrameEncode> frame;
            ComPtr<IPropertyBag2> properties;
            requireWindowsMediaSuccess(
                encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf()),
                "Create native media PNG frame");
            requireWindowsMediaSuccess(
                frame->Initialize(properties.Get()),
                "Initialize native media PNG frame");
            requireWindowsMediaSuccess(
                frame->SetSize(width, height),
                "Set native media PNG size");

            WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
            requireWindowsMediaSuccess(
                frame->SetPixelFormat(&pixelFormat),
                "Set native media PNG pixel format");
            if (!IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA))
                throw std::runtime_error{ "Windows PNG encoder rejected Rose's BGRA contact-sheet format." };

            const std::uint64_t stride64 = static_cast<std::uint64_t>(width) * 4u;
            if (stride64 > (std::numeric_limits<UINT>::max)()
                || pixels.size() > (std::numeric_limits<UINT>::max)())
                throw std::runtime_error{ "Native media contact sheet is too large to encode." };

            requireWindowsMediaSuccess(
                frame->WritePixels(
                    height,
                    static_cast<UINT>(stride64),
                    static_cast<UINT>(pixels.size()),
                    const_cast<BYTE*>(pixels.data())),
                "Write native media PNG pixels");
            requireWindowsMediaSuccess(frame->Commit(), "Commit native media PNG frame");
            requireWindowsMediaSuccess(encoder->Commit(), "Commit native media PNG encoder");

            STATSTG statistics{};
            requireWindowsMediaSuccess(
                stream->Stat(&statistics, STATFLAG_NONAME),
                "Measure native media PNG stream");
            const ULONGLONG size = statistics.cbSize.QuadPart;
            if (size == 0 || size > maximumBytes || size > (std::numeric_limits<ULONG>::max)())
                throw std::runtime_error{ "Native media PNG contact sheet exceeded Rose's safety limit." };

            LARGE_INTEGER start{};
            requireWindowsMediaSuccess(
                stream->Seek(start, STREAM_SEEK_SET, nullptr),
                "Rewind native media PNG stream");
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            ULONG read{ 0 };
            requireWindowsMediaSuccess(
                stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read),
                "Read native media PNG stream");
            if (read != bytes.size())
                throw std::runtime_error{ "Native media PNG stream ended unexpectedly." };
            return bytes;
        }

        [[nodiscard]] std::vector<std::uint8_t> makeNativeContactSheet(
            const std::vector<NativeDecodedFrame>& frames,
            const std::size_t maximumBytes)
        {
            if (frames.empty())
                throw std::runtime_error{ "No native media frames were available for the contact sheet." };

            const std::size_t count = frames.size();
            const std::uint32_t columns = static_cast<std::uint32_t>(
                count <= 2u ? count : (count <= 4u ? 2u : 3u));
            const std::uint32_t rows = static_cast<std::uint32_t>((count + columns - 1u) / columns);
            std::uint32_t cellWidth{ 1u };
            std::uint32_t cellHeight{ 1u };
            for (const auto& frame : frames)
            {
                cellWidth = std::max(cellWidth, frame.width);
                cellHeight = std::max(cellHeight, frame.height);
            }

            constexpr std::uint32_t padding = 8u;
            constexpr std::uint32_t margin = 8u;
            const std::uint32_t sheetWidth = margin * 2u + columns * cellWidth + (columns - 1u) * padding;
            const std::uint32_t sheetHeight = margin * 2u + rows * cellHeight + (rows - 1u) * padding;
            const std::uint64_t pixelBytes = static_cast<std::uint64_t>(sheetWidth)
                * static_cast<std::uint64_t>(sheetHeight) * 4u;
            if (pixelBytes > 128ull * 1024ull * 1024ull
                || pixelBytes > (std::numeric_limits<std::size_t>::max)())
                throw std::runtime_error{ "Native media contact sheet dimensions exceed Rose's memory limit." };

            std::vector<std::uint8_t> sheet(static_cast<std::size_t>(pixelBytes), 28u);
            for (std::size_t index = 0; index < frames.size(); ++index)
            {
                const auto& frame = frames[index];
                const std::uint32_t column = static_cast<std::uint32_t>(index % columns);
                const std::uint32_t row = static_cast<std::uint32_t>(index / columns);
                const std::uint32_t originX = margin + column * (cellWidth + padding)
                    + (cellWidth - frame.width) / 2u;
                const std::uint32_t originY = margin + row * (cellHeight + padding)
                    + (cellHeight - frame.height) / 2u;

                for (std::uint32_t y = 0; y < frame.height; ++y)
                {
                    const std::size_t sourceOffset = static_cast<std::size_t>(y) * frame.width * 4u;
                    const std::size_t destinationOffset =
                        (static_cast<std::size_t>(originY + y) * sheetWidth + originX) * 4u;
                    std::copy_n(
                        frame.bgra.data() + sourceOffset,
                        static_cast<std::size_t>(frame.width) * 4u,
                        sheet.data() + destinationOffset);
                }
            }
            return encodeBgraPng(sheet, sheetWidth, sheetHeight, maximumBytes);
        }

        class WindowsMediaFoundationService final : public IMediaService
        {
        public:
            explicit WindowsMediaFoundationService(FfmpegMediaConfig config)
                : config_{ std::move(config) }
            {}

            [[nodiscard]] bool available() const noexcept override { return true; }

            [[nodiscard]] std::string availabilityMessage() const override
            {
                return "Windows Media Foundation native fallback is available; actual codec/container support depends on codecs installed in Windows.";
            }

            [[nodiscard]] MediaMetadata probe(const std::filesystem::path& path) const override
            {
                validateMediaPath(path, config_.maximumSourceBytes);
                WindowsMediaFoundationSession session;
                ComPtr<IMFSourceReader> reader = createNativeSourceReader(path);
                return probeNativeReader(*reader.Get(), path);
            }

            [[nodiscard]] MediaInspection inspect(
                const std::filesystem::path& path,
                const std::size_t maximumFrames) const override
            {
                validateMediaPath(path, config_.maximumSourceBytes);
                WindowsMediaFoundationSession session;
                ComPtr<IMFSourceReader> reader = createNativeSourceReader(path);

                MediaInspection result;
                result.metadata = probeNativeReader(*reader.Get(), path);
                if (result.metadata.width <= 0 || result.metadata.height <= 0)
                    throw std::runtime_error{ "Windows Media Foundation did not report valid video dimensions." };

                ComPtr<IMFMediaType> outputType;
                requireWindowsMediaSuccess(
                    MFCreateMediaType(outputType.GetAddressOf()),
                    "Create native media RGB32 type");
                requireWindowsMediaSuccess(
                    outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video),
                    "Set native media major type");
                requireWindowsMediaSuccess(
                    outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32),
                    "Set native media RGB32 subtype");
                requireWindowsMediaSuccess(
                    reader->SetCurrentMediaType(
                        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                        nullptr,
                        outputType.Get()),
                    "Request native RGB32 video frames");

                const std::size_t requestedFrames = std::max<std::size_t>(
                    1u,
                    std::min({ maximumFrames, config_.maximumFrames, static_cast<std::size_t>(6u) }));
                const std::size_t sampleCount = result.metadata.durationSeconds > 0.15
                    ? requestedFrames
                    : 1u;

                std::vector<NativeDecodedFrame> frames;
                frames.reserve(sampleCount);
                result.sampleTimestamps.reserve(sampleCount);
                const std::uint64_t totalDuration100ns = static_cast<std::uint64_t>(
                    std::max(0.0, result.metadata.durationSeconds) * 10'000'000.0);

                for (std::size_t index = 0; index < sampleCount; ++index)
                {
                    const double fraction = sampleCount == 1u
                        ? 0.0
                        : (static_cast<double>(index) + 0.5) / static_cast<double>(sampleCount);
                    const std::uint64_t timestamp = sampleCount == 1u
                        ? 0u
                        : static_cast<std::uint64_t>(
                            static_cast<long double>(totalDuration100ns) * fraction);
                    result.sampleTimestamps.push_back(static_cast<double>(timestamp) / 10'000'000.0);
                    frames.push_back(readNativeFrame(
                        *reader.Get(),
                        timestamp,
                        static_cast<std::uint32_t>(result.metadata.width),
                        static_cast<std::uint32_t>(result.metadata.height)));
                }

                result.contactSheetPng = makeNativeContactSheet(frames, config_.maximumFrameBytes);
                return result;
            }

        private:
            FfmpegMediaConfig config_;
        };
#else
        class WindowsMediaFoundationService final : public IMediaService
        {
        public:
            explicit WindowsMediaFoundationService(FfmpegMediaConfig) {}
            [[nodiscard]] bool available() const noexcept override { return false; }
            [[nodiscard]] std::string availabilityMessage() const override
            {
                return "Windows Media Foundation fallback is available only on Windows.";
            }
            [[nodiscard]] MediaMetadata probe(const std::filesystem::path&) const override
            {
                throw std::runtime_error{ availabilityMessage() };
            }
            [[nodiscard]] MediaInspection inspect(const std::filesystem::path&, std::size_t) const override
            {
                throw std::runtime_error{ availabilityMessage() };
            }
        };
#endif
    }

    FfmpegMediaService::FfmpegMediaService(FfmpegMediaConfig config)
        : config_{ std::move(config) }
    {
#ifdef _WIN32
        ffmpeg_ = resolveExecutable(config_.ffmpegExecutable, L"ffmpeg.exe");
        ffprobe_ = resolveExecutable(config_.ffprobeExecutable, L"ffprobe.exe");
#else
        ffmpeg_ = config_.ffmpegExecutable;
        ffprobe_ = config_.ffprobeExecutable;
#endif
    }

    bool FfmpegMediaService::available() const noexcept
    {
#ifdef _WIN32
        return !ffmpeg_.empty() && !ffprobe_.empty();
#else
        return false;
#endif
    }

    std::string FfmpegMediaService::availabilityMessage() const
    {
        if (available()) return "FFmpeg media inspection is available.";
        return "FFmpeg media inspection is unavailable. Place ffmpeg.exe and ffprobe.exe in tools/ffmpeg/bin or install them on PATH.";
    }

    MediaMetadata FfmpegMediaService::probe(const std::filesystem::path& path) const
    {
        validateMediaPath(path, config_.maximumSourceBytes);
        if (!available()) throw std::runtime_error{ availabilityMessage() };
#ifdef _WIN32
        TemporaryDirectory temporary;
        const auto jsonPath = temporary.path() / "probe.json";
        runProcess(ffprobe_, {
            L"-v", L"error",
            L"-show_entries", L"format=format_name,duration,size:stream=index,codec_type,codec_name,width,height,avg_frame_rate",
            L"-of", L"json",
            L"-o", jsonPath.wstring(),
            path.wstring()
        }, config_.probeTimeoutMilliseconds);

        const integrations::json::Value root = integrations::json::parse(readTextFile(jsonPath));
        MediaMetadata metadata;
        metadata.path = path;

        if (const auto* formatValue = root.find("format"); formatValue && formatValue->object())
        {
            metadata.formatName = integrations::json::stringOr(*formatValue, "format_name");
            metadata.durationSeconds = numberOrStringDouble(*formatValue, "duration");
            metadata.sourceBytes = integerOrStringUint(*formatValue, "size");
        }
        if (metadata.sourceBytes == 0)
        {
            std::error_code error;
            metadata.sourceBytes = std::filesystem::file_size(path, error);
        }

        if (const auto* streamsValue = root.find("streams"))
        {
            if (const auto* streams = streamsValue->array())
            {
                for (const auto& stream : *streams)
                {
                    const std::string type = integrations::json::stringOr(stream, "codec_type");
                    if (type == "video" && metadata.videoCodec.empty())
                    {
                        metadata.videoCodec = integrations::json::stringOr(stream, "codec_name");
                        metadata.width = static_cast<int>(integrations::json::integerOr(stream, "width", 0));
                        metadata.height = static_cast<int>(integrations::json::integerOr(stream, "height", 0));
                        metadata.frameRate = integrations::json::stringOr(stream, "avg_frame_rate");
                    }
                    else if (type == "audio" && !metadata.hasAudio)
                    {
                        metadata.hasAudio = true;
                        metadata.audioCodec = integrations::json::stringOr(stream, "codec_name");
                    }
                }
            }
        }
        if (metadata.videoCodec.empty()) throw std::runtime_error{ "Media file contains no decodable video stream." };
        return metadata;
#else
        (void)path;
        throw std::runtime_error{ availabilityMessage() };
#endif
    }

    MediaInspection FfmpegMediaService::inspect(
        const std::filesystem::path& path,
        const std::size_t maximumFrames) const
    {
        MediaInspection result;
        result.metadata = probe(path);
#ifdef _WIN32
        const std::size_t requestedFrames = std::max<std::size_t>(1u,
            std::min({ maximumFrames, config_.maximumFrames, static_cast<std::size_t>(6u) }));

        // Extremely short/unknown-duration media is treated as a single visual
        // sample. Otherwise we sample the centers of evenly sized time buckets.
        const std::size_t sampleCount = result.metadata.durationSeconds > 0.15
            ? requestedFrames
            : 1u;
        result.sampleTimestamps.reserve(sampleCount);
        if (sampleCount == 1u)
        {
            result.sampleTimestamps.push_back(0.0);
        }
        else
        {
            for (std::size_t i = 0; i < sampleCount; ++i)
            {
                const double fraction =
                    (static_cast<double>(i) + 0.5) / static_cast<double>(sampleCount);
                result.sampleTimestamps.push_back(
                    std::max(0.0, result.metadata.durationSeconds * fraction));
            }
        }

        TemporaryDirectory temporary;
        const auto contactSheetPath = temporary.path() / "contact-sheet.png";

        std::wstring filter;
        if (sampleCount == 1u)
        {
            filter = L"scale=w=min(768\\,iw):h=-2";
        }
        else
        {
            const std::size_t columns = sampleCount <= 2u ? sampleCount : (sampleCount <= 4u ? 2u : 3u);
            const std::size_t rows = (sampleCount + columns - 1u) / columns;

            // fps gives us a bounded chronological sampling pass; tile combines
            // those frames into one image so Rose's process-isolated vision model
            // is loaded only once per media inspection. The timestamps recorded
            // above are intentionally approximate bucket centers.
            const double samplingFps = static_cast<double>(sampleCount)
                / std::max(0.001, result.metadata.durationSeconds);
            std::ostringstream fpsText;
            fpsText << std::fixed << std::setprecision(8) << samplingFps;

            const std::string filterUtf8 =
                "fps=fps=" + fpsText.str()
                + ",scale=w=min(512\\,iw):h=-2"
                + ",tile=layout=" + std::to_string(columns) + "x" + std::to_string(rows)
                + ":nb_frames=" + std::to_string(sampleCount)
                + ":padding=8:margin=8";
            filter.assign(filterUtf8.begin(), filterUtf8.end());
        }

        runProcess(ffmpeg_, {
            L"-v", L"error",
            L"-i", path.wstring(),
            L"-an",
            L"-vf", filter,
            L"-frames:v", L"1",
            L"-y", contactSheetPath.wstring()
        }, config_.frameTimeoutMilliseconds);

        result.contactSheetPng =
            readBinaryFileBounded(contactSheetPath, config_.maximumFrameBytes);
        return result;
#else
        (void)maximumFrames;
        throw std::runtime_error{ availabilityMessage() };
#endif
    }

    LocalMediaService::LocalMediaService(FfmpegMediaConfig ffmpegConfig)
        : preferred_{ std::make_unique<FfmpegMediaService>(ffmpegConfig) }
        , fallback_{ std::make_unique<WindowsMediaFoundationService>(std::move(ffmpegConfig)) }
    {}

    LocalMediaService::~LocalMediaService() = default;

    bool LocalMediaService::available() const noexcept
    {
        return (preferred_ && preferred_->available())
            || (fallback_ && fallback_->available());
    }

    std::string LocalMediaService::availabilityMessage() const
    {
        std::string message;
        if (preferred_)
        {
            message += preferred_->availabilityMessage();
        }
        if (fallback_)
        {
            if (!message.empty()) message += " ";
            message += fallback_->availabilityMessage();
        }
        return message.empty()
            ? "No local media inspection backend is configured."
            : message;
    }

    MediaMetadata LocalMediaService::probe(const std::filesystem::path& path) const
    {
        std::string preferredFailure;
        if (preferred_ && preferred_->available())
        {
            try
            {
                return preferred_->probe(path);
            }
            catch (const std::exception& exception)
            {
                preferredFailure = exception.what();
            }
        }

        if (fallback_ && fallback_->available())
        {
            try
            {
                return fallback_->probe(path);
            }
            catch (const std::exception& exception)
            {
                std::string message = "Local media metadata inspection failed.";
                if (!preferredFailure.empty())
                    message += " FFmpeg backend: " + preferredFailure;
                message += " Native Windows backend: ";
                message += exception.what();
                throw std::runtime_error{ message };
            }
        }

        if (!preferredFailure.empty())
            throw std::runtime_error{ "Local media metadata inspection failed: " + preferredFailure };
        throw std::runtime_error{ availabilityMessage() };
    }

    MediaInspection LocalMediaService::inspect(
        const std::filesystem::path& path,
        const std::size_t maximumFrames) const
    {
        std::string preferredFailure;
        if (preferred_ && preferred_->available())
        {
            try
            {
                return preferred_->inspect(path, maximumFrames);
            }
            catch (const std::exception& exception)
            {
                preferredFailure = exception.what();
            }
        }

        if (fallback_ && fallback_->available())
        {
            try
            {
                return fallback_->inspect(path, maximumFrames);
            }
            catch (const std::exception& exception)
            {
                std::string message = "Local media inspection failed.";
                if (!preferredFailure.empty())
                    message += " FFmpeg backend: " + preferredFailure;
                message += " Native Windows backend: ";
                message += exception.what();
                message += " If this codec is not installed in Windows, installing FFmpeg in tools/ffmpeg/bin remains the broad-compatibility fallback.";
                throw std::runtime_error{ message };
            }
        }

        if (!preferredFailure.empty())
            throw std::runtime_error{ "Local media inspection failed: " + preferredFailure };
        throw std::runtime_error{ availabilityMessage() };
    }

}
