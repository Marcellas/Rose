#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rose::media
{
    struct MediaMetadata
    {
        std::filesystem::path path;
        std::string formatName;
        double durationSeconds{ 0.0 };
        std::uintmax_t sourceBytes{ 0 };
        std::string videoCodec;
        int width{ 0 };
        int height{ 0 };
        std::string frameRate;
        bool hasAudio{ false };
        std::string audioCodec;
    };

    struct MediaInspection
    {
        MediaMetadata metadata;

        // Approximate timestamps represented by the contact-sheet cells in
        // left-to-right, top-to-bottom order.
        std::vector<double> sampleTimestamps;

        // One PNG contact sheet assembled locally by the active media backend. A
        // single semantic vision call can therefore compare representative moments
        // without reloading the vision model for every sampled frame.
        std::vector<std::uint8_t> contactSheetPng;
    };

    class IMediaService
    {
    public:
        virtual ~IMediaService() = default;

        [[nodiscard]] virtual bool available() const noexcept = 0;
        [[nodiscard]] virtual std::string availabilityMessage() const = 0;
        [[nodiscard]] virtual MediaMetadata probe(const std::filesystem::path& path) const = 0;
        [[nodiscard]] virtual MediaInspection inspect(
            const std::filesystem::path& path,
            std::size_t maximumFrames) const = 0;
    };

    struct FfmpegMediaConfig
    {
        std::filesystem::path ffmpegExecutable;
        std::filesystem::path ffprobeExecutable;
        std::uintmax_t maximumSourceBytes{ 8ull * 1024ull * 1024ull * 1024ull };
        std::size_t maximumFrameBytes{ 16u * 1024u * 1024u };
        std::size_t maximumFrames{ 6u };
        std::uint32_t probeTimeoutMilliseconds{ 30'000u };
        std::uint32_t frameTimeoutMilliseconds{ 45'000u };
    };

    // Optional local FFmpeg/ffprobe adapter. Rose does not link to FFmpeg and can
    // still start when the executables are absent. On Windows the adapter searches
    // Rose-local tool folders, common install folders, then PATH.
    class FfmpegMediaService final : public IMediaService
    {
    public:
        explicit FfmpegMediaService(FfmpegMediaConfig config = {});

        [[nodiscard]] bool available() const noexcept override;
        [[nodiscard]] std::string availabilityMessage() const override;
        [[nodiscard]] MediaMetadata probe(const std::filesystem::path& path) const override;
        [[nodiscard]] MediaInspection inspect(
            const std::filesystem::path& path,
            std::size_t maximumFrames) const override;

    private:
        FfmpegMediaConfig config_;
        std::filesystem::path ffmpeg_;
        std::filesystem::path ffprobe_;
    };


    // Provider-neutral local media facade. FFmpeg remains the preferred backend
    // when it is installed because its codec/container coverage is broader. On
    // Windows, Rose falls back to the operating system's Media Foundation stack
    // so ordinary media inspection does not require a separately installed tool.
    //
    // The facade owns both backend implementations. Callers depend only on
    // IMediaService, so a future libav/libarchive-style in-process provider can be
    // added without changing Agent tools or Project Knowledge readers.
    class LocalMediaService final : public IMediaService
    {
    public:
        explicit LocalMediaService(FfmpegMediaConfig ffmpegConfig = {});
        ~LocalMediaService() override;

        LocalMediaService(const LocalMediaService&) = delete;
        LocalMediaService& operator=(const LocalMediaService&) = delete;

        [[nodiscard]] bool available() const noexcept override;
        [[nodiscard]] std::string availabilityMessage() const override;
        [[nodiscard]] MediaMetadata probe(const std::filesystem::path& path) const override;
        [[nodiscard]] MediaInspection inspect(
            const std::filesystem::path& path,
            std::size_t maximumFrames) const override;

    private:
        std::unique_ptr<IMediaService> preferred_;
        std::unique_ptr<IMediaService> fallback_;
    };
}
