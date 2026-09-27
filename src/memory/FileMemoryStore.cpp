#include "memory/FileMemoryStore.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace rose::memory
{
    namespace
    {
        constexpr std::array<char, 8> recordMagic{
            'R', 'O', 'S', 'E',
            'M', 'E', 'M', '1'
        };

        constexpr std::uint32_t formatVersion{ 1 };
        constexpr std::uint32_t maximumContentBytes{ 64U * 1024U };
        constexpr std::uint32_t maximumSourceBytes{ 4U * 1024U };
        constexpr std::size_t maximumLoadedRecords{ 100000 };


        template<typename T>
        void appendScalar(
            std::string& destination,
            const T value)
        {
            static_assert(
                std::is_trivially_copyable_v<T>);

            const auto* bytes =
                reinterpret_cast<const char*>(&value);

            destination.append(
                bytes,
                sizeof(T));
        }


        template<typename T>
        [[nodiscard]]
        bool readScalar(
            std::ifstream& input,
            T& value)
        {
            static_assert(
                std::is_trivially_copyable_v<T>);

            input.read(
                reinterpret_cast<char*>(&value),
                sizeof(T));

            return
                input.gcount()
                == static_cast<std::streamsize>(sizeof(T));
        }


        [[nodiscard]]
        std::uint32_t checkedLength(
            const std::string_view text,
            const std::uint32_t maximum,
            const char* fieldName)
        {
            if (text.size() > maximum)
            {
                throw std::runtime_error{
                    std::string{ "Memory " }
                    + fieldName
                    + " exceeds Rose's persistence record limit."
                };
            }

            return static_cast<std::uint32_t>(text.size());
        }


        void ensureParentDirectory(
            const std::filesystem::path& path)
        {
            const std::filesystem::path parent =
                path.parent_path();

            if (parent.empty())
            {
                return;
            }

            std::error_code error;
            std::filesystem::create_directories(
                parent,
                error);

            if (error)
            {
                throw std::runtime_error{
                    "Could not create Rose memory directory: "
                    + error.message()
                };
            }
        }


        [[nodiscard]]
        bool validKind(
            const std::uint32_t rawKind) noexcept
        {
            return
                rawKind >= static_cast<std::uint32_t>(MemoryKind::ExplicitUser)
                && rawKind <= static_cast<std::uint32_t>(MemoryKind::DocumentSource);
        }
    }


    FileMemoryStore::FileMemoryStore(
        std::filesystem::path path)
        : path_{ std::move(path) }
    {
        if (path_.empty())
        {
            throw std::invalid_argument{
                "FileMemoryStore requires a path."
            };
        }
    }


    std::vector<MemoryRecord>
        FileMemoryStore::loadMemories()
    {
        std::vector<MemoryRecord> memories;

        if (!std::filesystem::exists(path_))
        {
            return memories;
        }

        std::ifstream input{
            path_,
            std::ios::binary
        };

        if (!input)
        {
            throw std::runtime_error{
                "Could not open Rose memory journal: "
                + path_.string()
            };
        }

        while (true)
        {
            std::array<char, recordMagic.size()> magic{};

            input.read(
                magic.data(),
                static_cast<std::streamsize>(magic.size()));

            const std::streamsize magicBytesRead =
                input.gcount();

            if (magicBytesRead == 0)
            {
                break;
            }

            // A crash during the final append must not destroy older memories.
            if (
                magicBytesRead
                != static_cast<std::streamsize>(magic.size()))
            {
                break;
            }

            if (magic != recordMagic)
            {
                throw std::runtime_error{
                    "Rose memory journal contains an invalid record marker."
                };
            }

            std::uint32_t version{ 0 };
            std::uint64_t id{ 0 };
            std::int64_t timestamp{ 0 };
            std::uint32_t rawKind{ 0 };
            std::uint32_t sourceBytes{ 0 };
            std::uint32_t contentBytes{ 0 };

            if (
                !readScalar(input, version)
                || !readScalar(input, id)
                || !readScalar(input, timestamp)
                || !readScalar(input, rawKind)
                || !readScalar(input, sourceBytes)
                || !readScalar(input, contentBytes))
            {
                break;
            }

            if (version != formatVersion)
            {
                throw std::runtime_error{
                    "Rose memory journal uses an unsupported format version."
                };
            }

            if (
                id == 0
                || !validKind(rawKind)
                || sourceBytes > maximumSourceBytes
                || contentBytes > maximumContentBytes)
            {
                throw std::runtime_error{
                    "Rose memory journal contains an invalid record header."
                };
            }

            MemoryRecord memory;
            memory.id = id;
            memory.unixTimeMilliseconds = timestamp;
            memory.kind = static_cast<MemoryKind>(rawKind);
            memory.source.resize(sourceBytes);
            memory.content.resize(contentBytes);

            if (sourceBytes > 0)
            {
                input.read(
                    memory.source.data(),
                    static_cast<std::streamsize>(sourceBytes));

                if (
                    input.gcount()
                    != static_cast<std::streamsize>(sourceBytes))
                {
                    break;
                }
            }

            if (contentBytes > 0)
            {
                input.read(
                    memory.content.data(),
                    static_cast<std::streamsize>(contentBytes));

                if (
                    input.gcount()
                    != static_cast<std::streamsize>(contentBytes))
                {
                    break;
                }
            }

            memories.push_back(
                std::move(memory));

            if (memories.size() > maximumLoadedRecords)
            {
                throw std::runtime_error{
                    "Rose memory journal exceeds the v0.1 in-memory record limit."
                };
            }
        }

        return memories;
    }


    void FileMemoryStore::appendMemory(
        const MemoryRecord& memory)
    {
        if (memory.id == 0)
        {
            throw std::invalid_argument{
                "Memory record id must be non-zero."
            };
        }

        if (memory.content.empty())
        {
            throw std::invalid_argument{
                "Memory record content cannot be empty."
            };
        }

        const std::uint32_t sourceBytes =
            checkedLength(
                memory.source,
                maximumSourceBytes,
                "source");

        const std::uint32_t contentBytes =
            checkedLength(
                memory.content,
                maximumContentBytes,
                "content");

        ensureParentDirectory(path_);

        std::string record;

        constexpr std::size_t headerBytes =
            recordMagic.size()
            + sizeof(std::uint32_t)
            + sizeof(std::uint64_t)
            + sizeof(std::int64_t)
            + sizeof(std::uint32_t)
            + sizeof(std::uint32_t)
            + sizeof(std::uint32_t);

        record.reserve(
            headerBytes
            + memory.source.size()
            + memory.content.size());

        record.append(
            recordMagic.data(),
            recordMagic.size());

        appendScalar(record, formatVersion);
        appendScalar(record, memory.id);
        appendScalar(record, memory.unixTimeMilliseconds);
        appendScalar(
            record,
            static_cast<std::uint32_t>(memory.kind));
        appendScalar(record, sourceBytes);
        appendScalar(record, contentBytes);

        record.append(
            memory.source.data(),
            memory.source.size());

        record.append(
            memory.content.data(),
            memory.content.size());

        std::ofstream output{
            path_,
            std::ios::binary | std::ios::app
        };

        if (!output)
        {
            throw std::runtime_error{
                "Could not open Rose memory journal for writing: "
                + path_.string()
            };
        }

        output.write(
            record.data(),
            static_cast<std::streamsize>(record.size()));

        output.flush();

        if (!output)
        {
            throw std::runtime_error{
                "Could not persist Rose memory record."
            };
        }
    }


    const std::filesystem::path&
        FileMemoryStore::path() const noexcept
    {
        return path_;
    }

} // namespace rose::memory
