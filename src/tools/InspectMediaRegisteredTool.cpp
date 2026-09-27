#include "tools/InspectMediaRegisteredTool.h"

#include "files/FileFormatCatalog.h"
#include "media/MediaService.h"
#include "permissions/PermissionSystem.h"
#include "vision/IVisionProvider.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]] std::string formatSeconds(const double value)
        {
            std::ostringstream text;
            text << std::fixed << std::setprecision(2) << value;
            return text.str();
        }
    }

    InspectMediaRegisteredTool::InspectMediaRegisteredTool(
        permissions::PermissionSystem& permissions,
        media::IMediaService& mediaService,
        std::unique_ptr<vision::IVisionProvider> visionProvider)
        : permissions_{ permissions }
        , mediaService_{ mediaService }
        , visionProvider_{ std::move(visionProvider) }
        , descriptor_{
            .id = "inspect_media",
            .displayName = "Inspect Media",
            .description = "Inspect one exact video or animated-image file using Rose's local media backend (FFmpeg when available, Windows Media Foundation fallback) plus the local vision provider. Supports WebM/MP4/MOV/MKV/AVI/WMV/MPEG and animated GIF. Read-only and bounded; representative frames do not imply full audio/transcript understanding.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "path", .description = "Absolute path of the exact media file to inspect.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "instruction", .description = "Optional concise question about the media.", .type = ToolValueType::String, .required = false }
            }
        }
    {
        if (!visionProvider_) throw std::invalid_argument{ "InspectMediaRegisteredTool requires a vision provider." };
    }

    InspectMediaRegisteredTool::~InspectMediaRegisteredTool() = default;

    const ToolDescriptor& InspectMediaRegisteredTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult InspectMediaRegisteredTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
            throw std::invalid_argument{ "InspectMediaRegisteredTool received a different tool id." };
        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "path" && name != "instruction")
                throw std::invalid_argument{ "inspect_media does not accept argument '" + name + "'." };
        }
        const auto pathArg = request.arguments.find("path");
        if (pathArg == request.arguments.end() || pathArg->second.empty())
            throw std::invalid_argument{ "inspect_media requires argument 'path'." };

        const std::filesystem::path path{ pathArg->second };
        if (!path.is_absolute()) throw std::invalid_argument{ "inspect_media requires an absolute file path." };
        if (!files::isMediaFile(path)) throw std::invalid_argument{ "inspect_media requires a supported video or animated-image extension." };

        // ToolExecutionPolicy owns user confirmation; PermissionSystem still records
        // that this exact file was authorized before the media backend opens it.
        permissions_.grantReadOnce(path);
        if (!permissions_.consumeReadOnce(path))
            throw std::runtime_error{ "Media read permission could not be established." };
        if (!mediaService_.available()) throw std::runtime_error{ mediaService_.availabilityMessage() };

        const media::MediaInspection inspection = mediaService_.inspect(path, 5u);
        const auto instruction = request.arguments.find("instruction");
        const std::string userInstruction = instruction == request.arguments.end() || instruction->second.empty()
            ? "Describe what is visible and note important changes across this representative media frame."
            : instruction->second;

        std::string message = "Inspected media: " + path.string();
        message += "\nformat=" + inspection.metadata.formatName;
        message += "\nduration_seconds=" + formatSeconds(inspection.metadata.durationSeconds);
        message += "\nvideo_codec=" + inspection.metadata.videoCodec;
        message += "\nresolution=" + std::to_string(inspection.metadata.width) + "x" + std::to_string(inspection.metadata.height);
        if (!inspection.metadata.frameRate.empty()) message += "\nframe_rate=" + inspection.metadata.frameRate;
        message += "\nhas_audio=" + std::string{ inspection.metadata.hasAudio ? "true" : "false" };
        if (inspection.metadata.hasAudio) message += "\naudio_codec=" + inspection.metadata.audioCodec;
        message += "\nrepresentative_frames=" + std::to_string(inspection.sampleTimestamps.size());
        message += "\nNOTICE: Rose analyzed representative visual frames, not every frame and not spoken audio/transcription.";

        std::string visionFailure;
        bool semanticAnalysisSucceeded{ false };
        if (visionProvider_->available() && !inspection.contactSheetPng.empty())
        {
            try
            {
                std::string prompt = userInstruction;
                prompt += " The image is a chronological contact sheet sampled from one media file. ";
                prompt += "Read cells left-to-right, top-to-bottom. Approximate cell timestamps in that order are: ";
                for (std::size_t index = 0; index < inspection.sampleTimestamps.size(); ++index)
                {
                    if (index != 0) prompt += ", ";
                    prompt += formatSeconds(inspection.sampleTimestamps[index]);
                    prompt += "s";
                }
                prompt += ". Compare the cells and report only visual evidence; do not infer unheard dialogue or audio content.";

                const vision::VisionResult visual = visionProvider_->analyze(vision::VisionRequest{
                    .encodedImage = inspection.contactSheetPng,
                    .sourceExtension = ".png",
                    .userPrompt = prompt
                });
                if (!visual.text.empty())
                {
                    semanticAnalysisSucceeded = true;
                    message += "\n<rose_untrusted_media_contact_sheet>\n";
                    message += visual.text;
                    message += "\n</rose_untrusted_media_contact_sheet>";
                }
            }
            catch (const std::exception& exception)
            {
                visionFailure = exception.what();
            }
        }
        else if (!visionProvider_->available())
        {
            visionFailure = visionProvider_->availabilityMessage();
        }

        if (!semanticAnalysisSucceeded)
        {
            if (!visionFailure.empty()) message += "\nvision_notice=" + visionFailure;
            message += "\nNOTICE: Metadata was read, but semantic frame analysis was unavailable.";
        }

        return ToolResult{ .success = true, .message = std::move(message), .artifacts = {} };
    }
}
