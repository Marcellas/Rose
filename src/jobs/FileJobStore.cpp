#include "jobs/FileJobStore.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>

namespace rose::jobs
{
    namespace
    {
        constexpr std::array<char, 8> fileMagic{
            'R', 'O', 'S', 'E', 'J', 'B', '0', '1'
        };

        constexpr std::uint32_t formatVersion{ 1 };
        constexpr std::uint32_t maximumJobs{ 100'000 };
        constexpr std::uint32_t maximumStringBytes{ 4U * 1024U * 1024U };

        template<typename T>
        void writeScalar(std::ostream& output, const T value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            output.write(reinterpret_cast<const char*>(&value), sizeof(T));
            if (!output)
            {
                throw std::runtime_error{ "Could not write Rose background-job data." };
            }
        }

        template<typename T>
        T readScalar(std::istream& input)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            T value{};
            input.read(reinterpret_cast<char*>(&value), sizeof(T));
            if (!input)
            {
                throw std::runtime_error{ "Rose background-job data is truncated." };
            }
            return value;
        }

        void writeString(std::ostream& output, const std::string& text)
        {
            if (text.size() > maximumStringBytes)
            {
                throw std::runtime_error{ "Rose background-job text exceeds the persistence limit." };
            }

            writeScalar(output, static_cast<std::uint32_t>(text.size()));
            if (!text.empty())
            {
                output.write(text.data(), static_cast<std::streamsize>(text.size()));
                if (!output)
                {
                    throw std::runtime_error{ "Could not write Rose background-job text." };
                }
            }
        }

        std::string readString(std::istream& input)
        {
            const std::uint32_t size = readScalar<std::uint32_t>(input);
            if (size > maximumStringBytes)
            {
                throw std::runtime_error{ "Rose background-job data contains an invalid string size." };
            }

            std::string result(size, '\0');
            if (size > 0)
            {
                input.read(result.data(), static_cast<std::streamsize>(size));
                if (!input)
                {
                    throw std::runtime_error{ "Rose background-job data is truncated inside a string." };
                }
            }
            return result;
        }

        void writeOptionalString(
            std::ostream& output,
            const std::optional<std::string>& value)
        {
            writeScalar(output, static_cast<std::uint8_t>(value.has_value() ? 1 : 0));
            if (value)
            {
                writeString(output, *value);
            }
        }

        std::optional<std::string> readOptionalString(std::istream& input)
        {
            const std::uint8_t present = readScalar<std::uint8_t>(input);
            if (present > 1)
            {
                throw std::runtime_error{ "Rose background-job data contains an invalid optional flag." };
            }
            if (present == 0)
            {
                return std::nullopt;
            }
            return readString(input);
        }

        void ensureParentDirectory(const std::filesystem::path& path)
        {
            const std::filesystem::path parent = path.parent_path();
            if (parent.empty())
            {
                return;
            }

            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error)
            {
                throw std::runtime_error{
                    "Could not create Rose background-job directory: " + parent.string()
                };
            }
        }

        void replaceFile(
            const std::filesystem::path& temporary,
            const std::filesystem::path& destination)
        {
            std::filesystem::path backup = destination;
            backup += ".bak";

            std::error_code error;
            std::filesystem::remove(backup, error);
            error.clear();

            const bool destinationExists = std::filesystem::exists(destination, error);
            if (error)
            {
                throw std::runtime_error{ "Could not inspect Rose background-job persistence state." };
            }

            if (destinationExists)
            {
                std::filesystem::rename(destination, backup, error);
                if (error)
                {
                    throw std::runtime_error{ "Could not rotate Rose background-job persistence backup." };
                }
            }

            std::filesystem::rename(temporary, destination, error);
            if (error)
            {
                if (destinationExists)
                {
                    std::error_code restoreError;
                    std::filesystem::rename(backup, destination, restoreError);
                }
                throw std::runtime_error{ "Could not commit Rose background-job persistence file." };
            }

            if (destinationExists)
            {
                std::filesystem::remove(backup, error);
            }
        }
    }

    FileJobStore::FileJobStore(std::filesystem::path path)
        : path_{ std::move(path) }
    {
    }

    JobSnapshot FileJobStore::load()
    {
        std::filesystem::path backup = path_;
        backup += ".bak";

        std::error_code error;
        const bool primaryExists = std::filesystem::exists(path_, error);
        if (error)
        {
            throw std::runtime_error{ "Could not inspect Rose background-job persistence file." };
        }

        std::filesystem::path loadPath = path_;
        if (!primaryExists)
        {
            const bool backupExists = std::filesystem::exists(backup, error);
            if (error)
            {
                throw std::runtime_error{ "Could not inspect Rose background-job persistence backup." };
            }

            if (!backupExists)
            {
                return {};
            }
            loadPath = backup;
        }

        std::ifstream input{ loadPath, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "Could not open Rose background-job persistence file." };
        }

        std::array<char, fileMagic.size()> magic{};
        input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!input || magic != fileMagic)
        {
            throw std::runtime_error{ "Rose background-job data has an invalid file marker." };
        }

        const std::uint32_t version = readScalar<std::uint32_t>(input);
        if (version != formatVersion)
        {
            throw std::runtime_error{ "Rose background-job data uses an unsupported format version." };
        }

        const std::uint32_t count = readScalar<std::uint32_t>(input);
        if (count > maximumJobs)
        {
            throw std::runtime_error{ "Rose background-job data contains too many jobs." };
        }

        JobSnapshot snapshot;
        snapshot.jobs.reserve(count);

        for (std::uint32_t index = 0; index < count; ++index)
        {
            JobRecord job;
            job.id = readString(input);
            job.kind = static_cast<JobKind>(readScalar<std::uint8_t>(input));
            job.status = static_cast<JobStatus>(readScalar<std::uint8_t>(input));
            job.text = readString(input);
            job.lastError = readString(input);
            job.projectId = readOptionalString(input);
            job.discussionId = readOptionalString(input);
            job.dueUnixMilliseconds = readScalar<std::int64_t>(input);
            job.createdUnixMilliseconds = readScalar<std::int64_t>(input);
            job.updatedUnixMilliseconds = readScalar<std::int64_t>(input);

            if (job.kind != JobKind::Reminder)
            {
                throw std::runtime_error{ "Rose background-job data contains an unknown job kind." };
            }

            const auto statusValue = static_cast<std::uint8_t>(job.status);
            if (
                statusValue < static_cast<std::uint8_t>(JobStatus::Pending)
                || statusValue > static_cast<std::uint8_t>(JobStatus::Failed))
            {
                throw std::runtime_error{ "Rose background-job data contains an unknown job status." };
            }

            snapshot.jobs.push_back(std::move(job));
        }

        return snapshot;
    }

    void FileJobStore::save(const JobSnapshot& snapshot)
    {
        if (snapshot.jobs.size() > maximumJobs)
        {
            throw std::runtime_error{ "Rose background-job count exceeds the persistence limit." };
        }

        ensureParentDirectory(path_);

        std::filesystem::path temporary = path_;
        temporary += ".tmp";

        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        if (!output)
        {
            throw std::runtime_error{ "Could not open temporary Rose background-job persistence file." };
        }

        output.write(fileMagic.data(), static_cast<std::streamsize>(fileMagic.size()));
        if (!output)
        {
            throw std::runtime_error{ "Could not write Rose background-job file marker." };
        }

        writeScalar(output, formatVersion);
        writeScalar(output, static_cast<std::uint32_t>(snapshot.jobs.size()));

        for (const JobRecord& job : snapshot.jobs)
        {
            writeString(output, job.id);
            writeScalar(output, static_cast<std::uint8_t>(job.kind));
            writeScalar(output, static_cast<std::uint8_t>(job.status));
            writeString(output, job.text);
            writeString(output, job.lastError);
            writeOptionalString(output, job.projectId);
            writeOptionalString(output, job.discussionId);
            writeScalar(output, job.dueUnixMilliseconds);
            writeScalar(output, job.createdUnixMilliseconds);
            writeScalar(output, job.updatedUnixMilliseconds);
        }

        output.flush();
        if (!output)
        {
            throw std::runtime_error{ "Could not flush Rose background-job persistence data." };
        }
        output.close();

        replaceFile(temporary, path_);
    }

    const std::filesystem::path& FileJobStore::path() const noexcept
    {
        return path_;
    }
}
