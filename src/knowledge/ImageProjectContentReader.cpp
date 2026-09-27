#include "knowledge/ImageProjectContentReader.h"

#include "files/FileFormatCatalog.h"
#include "ocr/IOcrEngine.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rose::knowledge
{
    ImageProjectContentReader::ImageProjectContentReader(
        std::unique_ptr<ocr::IOcrEngine> ocrEngine)
        : ocrEngine_{ std::move(ocrEngine) }
    {
        if (!ocrEngine_) throw std::invalid_argument{ "ImageProjectContentReader requires an OCR provider object." };
    }

    ImageProjectContentReader::~ImageProjectContentReader() = default;

    bool ImageProjectContentReader::supports(
        const std::filesystem::path& path) const noexcept
    {
        return files::isImageFile(path);
    }

    ExtractedProjectContent ImageProjectContentReader::read(
        const std::filesystem::path& path,
        const std::uintmax_t sourceBytes) const
    {
        if (sourceBytes > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)()))
        {
            throw std::runtime_error{ "Image is too large to address safely." };
        }

        std::ifstream input{ path, std::ios::binary };
        if (!input) throw std::runtime_error{ "Could not open image." };
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sourceBytes));
        if (!bytes.empty())
        {
            input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
            {
                throw std::runtime_error{ "Could not read the complete image." };
            }
        }

        std::string text = "Image asset: " + path.filename().string() + ".";
        if (ocrEngine_->available())
        {
            const ocr::OcrResult recognized = ocrEngine_->recognizeEncodedImage(bytes, path.extension().string());
            if (!recognized.text.empty())
            {
                text += "\nVisible text (OCR; may contain recognition errors):\n" + recognized.text;
            }
        }
        text += "\nSemantic pixel analysis is intentionally performed on demand rather than during bulk project indexing.";

        return ExtractedProjectContent{
            .contentKind = "image",
            .readerId = std::string{ id() },
            .segments = { ProjectContentSegment{ .locator = "image", .text = std::move(text) } }
        };
    }
}
