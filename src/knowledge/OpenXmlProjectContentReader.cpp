#include "knowledge/OpenXmlProjectContentReader.h"

#include "files/FileFormatCatalog.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace rose::knowledge
{
    bool OpenXmlProjectContentReader::supports(
        const std::filesystem::path& path) const noexcept
    {
        return files::isOpenXmlOfficeFile(path);
    }

    ExtractedProjectContent OpenXmlProjectContentReader::read(
        const std::filesystem::path& path,
        const std::uintmax_t sourceBytes) const
    {
        if (sourceBytes > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)()))
        {
            throw std::runtime_error{ "Office document is too large to address safely." };
        }
        std::ifstream input{ path, std::ios::binary };
        if (!input) throw std::runtime_error{ "Could not open Office document." };
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sourceBytes));
        if (!bytes.empty())
        {
            input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
            {
                throw std::runtime_error{ "Could not read the complete Office document." };
            }
        }

        const auto extracted = extractor_.extract(bytes, path.extension().string());
        ExtractedProjectContent result;
        result.contentKind = extracted.contentKind;
        result.readerId = std::string{ id() };
        for (const auto& segment : extracted.segments)
        {
            result.segments.push_back({ segment.locator, segment.text });
        }
        return result;
    }
}
