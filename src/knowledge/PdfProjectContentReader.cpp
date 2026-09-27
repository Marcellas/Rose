#include "knowledge/PdfProjectContentReader.h"

#include "files/FileFormatCatalog.h"
#include "ocr/IOcrEngine.h"
#include "tools/ReadFileTool.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rose::knowledge
{
    namespace
    {
        [[nodiscard]]
        std::vector<std::uint8_t> readBinary(
            const std::filesystem::path& path,
            const std::uintmax_t sourceBytes)
        {
            if (sourceBytes > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)()))
            {
                throw std::runtime_error{ "PDF is too large to address safely." };
            }
            std::ifstream input{ path, std::ios::binary };
            if (!input) throw std::runtime_error{ "Could not open PDF." };
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sourceBytes));
            if (!bytes.empty())
            {
                input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
                {
                    throw std::runtime_error{ "Could not read the complete PDF." };
                }
            }
            return bytes;
        }
    }

    PdfProjectContentReader::PdfProjectContentReader(
        std::unique_ptr<ocr::IOcrEngine> ocrEngine)
        : ocrEngine_{ std::move(ocrEngine) }
        , extractor_{ tools::PdfTextExtractorConfig{
            .maximumExtractedUtf8Bytes = 256u * 1024u,
            .minimumEmbeddedNonWhitespaceCharacters = 24u,
            .maximumOcrPages = 32u,
            .ocrRenderDpi = 180.0f,
            .maximumOcrImageDimension = 2600
        } }
    {
        if (!ocrEngine_) throw std::invalid_argument{ "PdfProjectContentReader requires an OCR provider object." };
    }

    PdfProjectContentReader::~PdfProjectContentReader() = default;

    bool PdfProjectContentReader::supports(
        const std::filesystem::path& path) const noexcept
    {
        return files::isPdfFile(path);
    }

    ExtractedProjectContent PdfProjectContentReader::read(
        const std::filesystem::path& path,
        const std::uintmax_t sourceBytes) const
    {
        tools::ReadBinaryFileResult file{
            .path = path,
            .displayName = path.filename().string(),
            .bytes = readBinary(path, sourceBytes),
            .originalSize = sourceBytes
        };

        const tools::ExtractedPdfDocument pdf = extractor_.extract(file, *ocrEngine_);
        if (pdf.text.empty()) return { .contentKind = "pdf", .readerId = std::string{ id() }, .segments = {} };

        ExtractedProjectContent result;
        result.contentKind = "pdf";
        result.readerId = std::string{ id() };
        result.segments.push_back({ "pages=1-" + std::to_string(pdf.pageCount), pdf.text });
        return result;
    }
}
