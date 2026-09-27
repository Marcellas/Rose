#include "knowledge/MediaProjectContentReader.h"

#include "files/FileFormatCatalog.h"
#include "media/MediaService.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace rose::knowledge
{
    MediaProjectContentReader::MediaProjectContentReader(
        std::unique_ptr<media::IMediaService> mediaService)
        : mediaService_{ std::move(mediaService) }
    {
        if (!mediaService_) throw std::invalid_argument{ "MediaProjectContentReader requires a media service." };
    }

    MediaProjectContentReader::~MediaProjectContentReader() = default;

    bool MediaProjectContentReader::supports(const std::filesystem::path& path) const noexcept
    {
        return files::isMediaFile(path);
    }

    ExtractedProjectContent MediaProjectContentReader::read(
        const std::filesystem::path& path,
        const std::uintmax_t sourceBytes) const
    {
        std::ostringstream text;
        text << "Media asset: " << path.filename().string() << ".\n";
        text << "Source bytes: " << sourceBytes << ".\n";
        if (mediaService_->available())
        {
            try
            {
                const media::MediaMetadata metadata = mediaService_->probe(path);
                text << "Container: " << metadata.formatName << ".\n";
                text << std::fixed << std::setprecision(2)
                     << "Duration seconds: " << metadata.durationSeconds << ".\n";
                text << "Video codec: " << metadata.videoCodec << ".\n";
                text << "Resolution: " << metadata.width << "x" << metadata.height << ".\n";
                if (!metadata.frameRate.empty()) text << "Frame rate: " << metadata.frameRate << ".\n";
                text << "Audio stream: " << (metadata.hasAudio ? "yes" : "no") << ".\n";
                if (metadata.hasAudio) text << "Audio codec: " << metadata.audioCodec << ".\n";
            }
            catch (const std::exception& exception)
            {
                text << "Media metadata probe failed: " << exception.what() << "\n";
            }
        }
        else
        {
            text << "Detailed media metadata unavailable: " << mediaService_->availabilityMessage() << "\n";
        }
        text << "Semantic frame analysis is intentionally performed on demand rather than during bulk project indexing.";

        return ExtractedProjectContent{
            .contentKind = "media",
            .readerId = std::string{ id() },
            .segments = { ProjectContentSegment{ .locator = "media", .text = text.str() } }
        };
    }
}
