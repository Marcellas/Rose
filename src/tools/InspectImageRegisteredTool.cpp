#include "tools/InspectImageRegisteredTool.h"

#include "files/FileFormatCatalog.h"
#include "ocr/IOcrEngine.h"
#include "permissions/PermissionSystem.h"
#include "tools/ReadFileTool.h"
#include "vision/IVisionProvider.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace rose::tools
{
    InspectImageRegisteredTool::InspectImageRegisteredTool(
        permissions::PermissionSystem& permissions,
        ReadFileTool& readFileTool,
        std::unique_ptr<ocr::IOcrEngine> ocrEngine,
        std::unique_ptr<vision::IVisionProvider> visionProvider)
        : permissions_{ permissions }
        , readFileTool_{ readFileTool }
        , ocrEngine_{ std::move(ocrEngine) }
        , visionProvider_{ std::move(visionProvider) }
        , descriptor_{
            .id = "inspect_image",
            .displayName = "Inspect Image",
            .description = "Semantically inspect one exact image at an absolute path with Rose's local vision provider and OCR. Supports PNG/JPEG/BMP/TIFF/WebP when the configured decoder accepts the file; animated GIF is handled by inspect_media. Read-only and bounded.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "path", .description = "Absolute path of the exact image to inspect.", .type = ToolValueType::String, .required = true },
                ToolParameterDescriptor{ .name = "instruction", .description = "Optional concise question about what to inspect in the image.", .type = ToolValueType::String, .required = false }
            }
        }
    {
        if (!ocrEngine_ || !visionProvider_) throw std::invalid_argument{ "InspectImageRegisteredTool requires OCR and vision provider objects." };
    }

    InspectImageRegisteredTool::~InspectImageRegisteredTool() = default;

    const ToolDescriptor& InspectImageRegisteredTool::descriptor() const noexcept { return descriptor_; }

    ToolResult InspectImageRegisteredTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id) throw std::invalid_argument{ "InspectImageRegisteredTool received a different tool id." };
        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "path" && name != "instruction") throw std::invalid_argument{ "inspect_image does not accept argument '" + name + "'." };
        }
        const auto pathArg = request.arguments.find("path");
        if (pathArg == request.arguments.end() || pathArg->second.empty()) throw std::invalid_argument{ "inspect_image requires argument 'path'." };
        const std::filesystem::path path{ pathArg->second };
        if (!path.is_absolute()) throw std::invalid_argument{ "inspect_image requires an absolute file path." };
        if (!files::isImageFile(path)) throw std::invalid_argument{ "inspect_image requires a supported image extension." };

        permissions_.grantReadOnce(path);
        const ReadBinaryFileResult file = readFileTool_.readBinaryFile(path);

        std::string visionText;
        std::string visionError;
        if (visionProvider_->available())
        {
            try
            {
                const auto instruction = request.arguments.find("instruction");
                const std::string prompt = instruction == request.arguments.end() || instruction->second.empty()
                    ? "Describe this image accurately and identify important visible details."
                    : instruction->second;
                visionText = visionProvider_->analyze(vision::VisionRequest{
                    .encodedImage = file.bytes,
                    .sourceExtension = path.extension().string(),
                    .userPrompt = prompt
                }).text;
            }
            catch (const std::exception& exception) { visionError = exception.what(); }
        }
        else visionError = visionProvider_->availabilityMessage();

        std::string ocrText;
        std::string ocrError;
        if (ocrEngine_->available())
        {
            try { ocrText = ocrEngine_->recognizeEncodedImage(file.bytes, path.extension().string()).text; }
            catch (const std::exception& exception) { ocrError = exception.what(); }
        }
        else ocrError = ocrEngine_->availabilityMessage();

        if (visionText.empty() && ocrText.empty())
        {
            throw std::runtime_error{ "Rose could not interpret the image. Vision: " + visionError + " OCR: " + ocrError };
        }

        std::string message = "Inspected image: " + file.path.string();
        if (!visionText.empty()) message += "\n<rose_untrusted_visual_observation>\n" + visionText + "\n</rose_untrusted_visual_observation>";
        if (!ocrText.empty()) message += "\n<rose_untrusted_image_ocr>\n" + ocrText + "\n</rose_untrusted_image_ocr>";
        if (!visionError.empty()) message += "\nvision_notice=" + visionError;
        if (!ocrError.empty()) message += "\nocr_notice=" + ocrError;

        return ToolResult{ .success = true, .message = std::move(message), .artifacts = {} };
    }
}
