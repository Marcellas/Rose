#include "artifacts/ArtifactStore.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace rose::artifacts
{

    namespace
    {
        [[nodiscard]]
        std::string sanitizeStem(
            const std::string_view input)
        {
            std::string result;
            result.reserve(input.size());

            bool previousSeparator{ false };

            for (const char character : input)
            {
                const unsigned char value =
                    static_cast<unsigned char>(character);

                if (
                    std::isalnum(value) != 0
                    || character == '_'
                    || character == '-')
                {
                    result.push_back(character);
                    previousSeparator = false;
                }
                else if (!previousSeparator)
                {
                    result.push_back('-');
                    previousSeparator = true;
                }

                if (result.size() >= 48)
                {
                    break;
                }
            }

            while (
                !result.empty()
                && result.back() == '-')
            {
                result.pop_back();
            }

            if (result.empty())
            {
                result = "artifact";
            }

            return result;
        }


        [[nodiscard]]
        std::string sanitizeExtension(
            const std::string_view extension)
        {
            std::string result;
            result.reserve(extension.size() + 1);

            if (
                extension.empty()
                || extension.front() != '.')
            {
                result.push_back('.');
            }

            for (const char character : extension)
            {
                if (character == '.')
                {
                    if (result.empty())
                    {
                        result.push_back('.');
                    }

                    continue;
                }

                const unsigned char value =
                    static_cast<unsigned char>(character);

                if (std::isalnum(value) != 0)
                {
                    result.push_back(
                        static_cast<char>(
                            std::tolower(value)));
                }
            }

            if (
                result.size() < 2
                || result.size() > 12)
            {
                throw std::invalid_argument{
                    "Artifact extension is invalid."
                };
            }

            return result;
        }


        [[nodiscard]]
        std::string timestampStem()
        {
            const auto now =
                std::chrono::system_clock::now();

            const std::time_t nowTime =
                std::chrono::system_clock::to_time_t(now);

            std::tm local{};

#ifdef _WIN32
            localtime_s(
                &local,
                &nowTime);
#else
            localtime_r(
                &nowTime,
                &local);
#endif

            std::ostringstream stream;
            stream
                << std::put_time(
                    &local,
                    "%Y%m%d-%H%M%S");

            return stream.str();
        }
    }


    ArtifactStore::ArtifactStore(
        std::filesystem::path rootDirectory)
        : rootDirectory_{
            std::filesystem::absolute(
                std::move(rootDirectory))
                .lexically_normal()
        }
    {
        if (rootDirectory_.empty())
        {
            throw std::invalid_argument{
                "ArtifactStore requires a root directory."
            };
        }

        std::filesystem::create_directories(
            rootDirectory_);
    }


    const std::filesystem::path&
    ArtifactStore::rootDirectory() const noexcept
    {
        return rootDirectory_;
    }


    std::filesystem::path ArtifactStore::allocatePath(
        const std::string_view suggestedStem,
        const std::string_view extension) const
    {
        static std::atomic<std::uint64_t> sequence{
            0
        };

        const std::string safeStem =
            sanitizeStem(suggestedStem);

        const std::string safeExtension =
            sanitizeExtension(extension);

        // The timestamp makes the directory human-friendly while the atomic
        // sequence prevents same-second collisions inside this Rose process.
        for (int attempt = 0; attempt < 1000; ++attempt)
        {
            const std::uint64_t id =
                sequence.fetch_add(
                    1,
                    std::memory_order_relaxed);

            const std::filesystem::path candidate =
                rootDirectory_
                / (
                    timestampStem()
                    + "-"
                    + safeStem
                    + "-"
                    + std::to_string(id)
                    + safeExtension);

            if (!std::filesystem::exists(candidate))
            {
                return candidate;
            }
        }

        throw std::runtime_error{
            "Could not allocate a unique Rose artifact path."
        };
    }


    Artifact ArtifactStore::finalize(
        const std::filesystem::path& path,
        const std::string_view displayName,
        const std::string_view mediaType,
        const ArtifactKind kind) const
    {
        const std::filesystem::path absolutePath =
            std::filesystem::absolute(path)
                .lexically_normal();

        if (!std::filesystem::exists(absolutePath))
        {
            throw std::runtime_error{
                "Generated artifact does not exist: "
                + absolutePath.string()
            };
        }

        if (!std::filesystem::is_regular_file(absolutePath))
        {
            throw std::runtime_error{
                "Generated artifact is not a regular file: "
                + absolutePath.string()
            };
        }

        if (
            std::filesystem::file_size(absolutePath)
            == 0)
        {
            throw std::runtime_error{
                "Generated artifact is empty: "
                + absolutePath.string()
            };
        }

        Artifact artifact;
        artifact.path = absolutePath;
        artifact.displayName =
            displayName.empty()
                ? absolutePath.filename().string()
                : std::string{ displayName };
        artifact.mediaType =
            std::string{ mediaType };
        artifact.kind = kind;

        return artifact;
    }

} // namespace rose::artifacts
