#include "media/MediaService.h"
#include "permissions/PermissionSystem.h"
#include "tools/InspectMediaRegisteredTool.h"
#include "vision/IVisionProvider.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void require(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error{ message };
    }

    class FakeMediaService final : public rose::media::IMediaService
    {
    public:
        [[nodiscard]] bool available() const noexcept override { return true; }
        [[nodiscard]] std::string availabilityMessage() const override { return "fake available"; }

        [[nodiscard]] rose::media::MediaMetadata probe(
            const std::filesystem::path& path) const override
        {
            return rose::media::MediaMetadata{
                .path = path,
                .formatName = "matroska,webm",
                .durationSeconds = 12.5,
                .sourceBytes = 1234,
                .videoCodec = "vp9",
                .width = 1280,
                .height = 720,
                .frameRate = "30/1",
                .hasAudio = true,
                .audioCodec = "opus"
            };
        }

        [[nodiscard]] rose::media::MediaInspection inspect(
            const std::filesystem::path& path,
            const std::size_t maximumFrames) const override
        {
            require(maximumFrames > 0, "media tool should request representative frames");
            rose::media::MediaInspection result;
            result.metadata = probe(path);
            result.sampleTimestamps = { 3.25, 9.75 };
            result.contactSheetPng = { 1, 2, 3, 4 };
            return result;
        }
    };

    class FakeVisionProvider final : public rose::vision::IVisionProvider
    {
    public:
        [[nodiscard]] bool available() const noexcept override { return true; }
        [[nodiscard]] std::string availabilityMessage() const override { return "fake vision available"; }

        [[nodiscard]] rose::vision::VisionResult analyze(
            const rose::vision::VisionRequest& request) override
        {
            require(request.sourceExtension == ".png", "media frames should be normalized to PNG");
            require(!request.encodedImage.empty(), "representative frame bytes should reach vision");
            require(request.userPrompt.find("3.25") != std::string_view::npos,
                    "contact-sheet prompt should carry first timestamp provenance");
            require(request.userPrompt.find("9.75") != std::string_view::npos,
                    "contact-sheet prompt should carry later timestamp provenance");
            require(request.userPrompt.find("left-to-right") != std::string_view::npos,
                    "contact-sheet prompt should explain chronological cell order");
            return rose::vision::VisionResult{ .text = "A red vehicle moves through the frame.", .truncated = false };
        }
    };
}

int main()
{
    try
    {
        rose::permissions::PermissionSystem permissions;
        FakeMediaService media;
        rose::tools::InspectMediaRegisteredTool tool{
            permissions,
            media,
            std::make_unique<FakeVisionProvider>()
        };

        // Build the synthetic fixture path through std::filesystem so this test
        // obeys the same absolute-path contract on both Windows and POSIX.
        // The fake media service never opens the file, so it does not need to exist.
        const std::filesystem::path samplePath =
            std::filesystem::temp_directory_path() / "rose-media-tools-test.webm";
        require(samplePath.is_absolute(), "media test fixture path must be absolute");

        const rose::tools::ToolResult result = tool.execute(rose::tools::ToolRequest{
            .toolId = "inspect_media",
            .arguments = {
                { "path", samplePath.string() },
                { "instruction", "Tell me what happens in this clip." }
            }
        });

        require(result.success, "media inspection should succeed");
        require(result.message.find("video_codec=vp9") != std::string::npos,
                "media observation should include video metadata");
        require(result.message.find("audio_codec=opus") != std::string::npos,
                "media observation should include audio metadata");
        require(result.message.find("A red vehicle") != std::string::npos,
                "media observation should include semantic frame evidence");
        require(result.message.find("representative visual frames") != std::string::npos,
                "media observation should disclose sampling limitations");

        std::cout << "Rose MediaTools tests: PASS\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rose MediaTools tests: FAIL: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
