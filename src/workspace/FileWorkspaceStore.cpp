#include "workspace/FileWorkspaceStore.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace rose::workspace
{

    namespace
    {
        constexpr std::array<char, 8> fileMagic{
            'R', 'O', 'S', 'E', 'W', 'S', '0', '1'
        };

        constexpr std::uint32_t formatVersion{ 2 };
        constexpr std::uint32_t oldestSupportedFormatVersion{ 1 };

        // Defensive bounds. These are intentionally far above the expected MVP
        // usage but prevent corrupt files from requesting absurd allocations.
        constexpr std::uint32_t maximumProjects{ 10'000 };
        constexpr std::uint32_t maximumDiscussions{ 100'000 };
        constexpr std::uint32_t maximumListEntries{ 100'000 };
        constexpr std::uint32_t maximumStringBytes{ 4U * 1024U * 1024U };


        template<typename T>
        void writeScalar(
            std::ostream& output,
            const T value)
        {
            static_assert(std::is_trivially_copyable_v<T>);

            output.write(
                reinterpret_cast<const char*>(&value),
                sizeof(T));

            if (!output)
            {
                throw std::runtime_error{
                    "Could not write Rose workspace metadata."
                };
            }
        }


        template<typename T>
        T readScalar(
            std::istream& input)
        {
            static_assert(std::is_trivially_copyable_v<T>);

            T value{};

            input.read(
                reinterpret_cast<char*>(&value),
                sizeof(T));

            if (!input)
            {
                throw std::runtime_error{
                    "Rose workspace metadata is truncated."
                };
            }

            return value;
        }


        std::uint32_t checkedSize(
            const std::size_t size,
            const std::uint32_t maximum,
            const std::string_view what)
        {
            if (size > maximum)
            {
                throw std::runtime_error{
                    std::string{ what }
                    + " exceeds Rose's workspace persistence limit."
                };
            }

            return static_cast<std::uint32_t>(size);
        }


        void writeString(
            std::ostream& output,
            const std::string_view text)
        {
            const std::uint32_t size =
                checkedSize(
                    text.size(),
                    maximumStringBytes,
                    "Workspace string");

            writeScalar(output, size);

            if (size == 0)
            {
                return;
            }

            output.write(
                text.data(),
                static_cast<std::streamsize>(size));

            if (!output)
            {
                throw std::runtime_error{
                    "Could not write Rose workspace string."
                };
            }
        }


        std::string readString(
            std::istream& input)
        {
            const std::uint32_t size =
                readScalar<std::uint32_t>(input);

            if (size > maximumStringBytes)
            {
                throw std::runtime_error{
                    "Rose workspace metadata contains an invalid string size."
                };
            }

            std::string value(size, '\0');

            if (size > 0)
            {
                input.read(
                    value.data(),
                    static_cast<std::streamsize>(size));

                if (!input)
                {
                    throw std::runtime_error{
                        "Rose workspace metadata is truncated inside a string."
                    };
                }
            }

            return value;
        }


        void writeStringList(
            std::ostream& output,
            const std::vector<std::string>& values)
        {
            const std::uint32_t count =
                checkedSize(
                    values.size(),
                    maximumListEntries,
                    "Workspace list");

            writeScalar(output, count);

            for (const std::string& value : values)
            {
                writeString(output, value);
            }
        }


        std::vector<std::string> readStringList(
            std::istream& input)
        {
            const std::uint32_t count =
                readScalar<std::uint32_t>(input);

            if (count > maximumListEntries)
            {
                throw std::runtime_error{
                    "Rose workspace metadata contains an invalid list size."
                };
            }

            std::vector<std::string> values;
            values.reserve(count);

            for (std::uint32_t index = 0; index < count; ++index)
            {
                values.push_back(readString(input));
            }

            return values;
        }


        void writeUint64List(
            std::ostream& output,
            const std::vector<std::uint64_t>& values)
        {
            const std::uint32_t count =
                checkedSize(
                    values.size(),
                    maximumListEntries,
                    "Workspace integer list");

            writeScalar(output, count);

            for (const std::uint64_t value : values)
            {
                writeScalar(output, value);
            }
        }


        std::vector<std::uint64_t> readUint64List(
            std::istream& input)
        {
            const std::uint32_t count =
                readScalar<std::uint32_t>(input);

            if (count > maximumListEntries)
            {
                throw std::runtime_error{
                    "Rose workspace metadata contains an invalid integer-list size."
                };
            }

            std::vector<std::uint64_t> values;
            values.reserve(count);

            for (std::uint32_t index = 0; index < count; ++index)
            {
                values.push_back(
                    readScalar<std::uint64_t>(input));
            }

            return values;
        }


        void writeSettings(
            std::ostream& output,
            const std::vector<ProjectSetting>& settings)
        {
            const std::uint32_t count =
                checkedSize(
                    settings.size(),
                    maximumListEntries,
                    "Project setting count");

            writeScalar(output, count);

            for (const ProjectSetting& setting : settings)
            {
                writeString(output, setting.key);
                writeString(output, setting.value);
            }
        }


        std::vector<ProjectSetting> readSettings(
            std::istream& input)
        {
            const std::uint32_t count =
                readScalar<std::uint32_t>(input);

            if (count > maximumListEntries)
            {
                throw std::runtime_error{
                    "Rose workspace metadata contains too many project settings."
                };
            }

            std::vector<ProjectSetting> settings;
            settings.reserve(count);

            for (std::uint32_t index = 0; index < count; ++index)
            {
                settings.push_back(
                    ProjectSetting{
                        .key = readString(input),
                        .value = readString(input)
                    });
            }

            return settings;
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
            std::filesystem::create_directories(parent, error);

            if (error)
            {
                throw std::runtime_error{
                    std::string{
                        "Could not create Rose workspace directory: "
                    }
                    + error.message()
                };
            }
        }


        std::filesystem::path backupPathFor(
            const std::filesystem::path& destination)
        {
            std::filesystem::path backup = destination;
            backup += ".bak";
            return backup;
        }


        void replaceFile(
            const std::filesystem::path& temporary,
            const std::filesystem::path& destination)
        {
            const std::filesystem::path backup =
                backupPathFor(destination);

            std::error_code error;

            const bool hadDestination =
                std::filesystem::exists(destination, error);

            if (error)
            {
                throw std::runtime_error{
                    "Could not inspect existing Rose workspace metadata."
                };
            }

            if (hadDestination)
            {
                // Only a destination + backup pair makes the old backup stale.
                // If the destination is missing, the backup may be our sole
                // recoverable snapshot from an interrupted previous save.
                std::filesystem::remove(backup, error);
                error.clear();

                std::filesystem::rename(destination, backup, error);

                if (error)
                {
                    throw std::runtime_error{
                        std::string{
                            "Could not stage existing Rose workspace metadata: "
                        }
                        + error.message()
                    };
                }
            }

            std::filesystem::rename(temporary, destination, error);

            if (error)
            {
                // Best-effort rollback. Keep the original error for the caller.
                const std::string activationError = error.message();

                if (hadDestination)
                {
                    std::error_code rollbackError;
                    std::filesystem::rename(
                        backup,
                        destination,
                        rollbackError);
                }

                throw std::runtime_error{
                    std::string{
                        "Could not activate Rose workspace metadata: "
                    }
                    + activationError
                };
            }

            if (hadDestination)
            {
                std::filesystem::remove(backup, error);
                // A stale backup is harmless. The newly activated destination is
                // already authoritative, so cleanup failure is not fatal.
            }
        }


    } // namespace


    FileWorkspaceStore::FileWorkspaceStore(
        std::filesystem::path path)
        : path_{ std::move(path) }
    {
        if (path_.empty())
        {
            throw std::invalid_argument{
                "FileWorkspaceStore requires a path."
            };
        }
    }


    WorkspaceSnapshot FileWorkspaceStore::load()
    {
        std::filesystem::path loadPath = path_;

        if (!std::filesystem::exists(loadPath))
        {
            const std::filesystem::path backup =
                backupPathFor(path_);

            if (!std::filesystem::exists(backup))
            {
                return {};
            }

            // A backup without the primary file means Rose was interrupted
            // between the two final rename operations of save(). Recover from
            // the last complete snapshot instead of treating this as first launch.
            loadPath = backup;
        }

        std::ifstream input{
            loadPath,
            std::ios::binary
        };

        if (!input)
        {
            throw std::runtime_error{
                std::string{
                    "Could not open Rose workspace metadata: "
                }
                + path_.string()
            };
        }

        std::array<char, fileMagic.size()> magic{};

        input.read(
            magic.data(),
            static_cast<std::streamsize>(magic.size()));

        if (!input || magic != fileMagic)
        {
            throw std::runtime_error{
                "Rose workspace metadata has an invalid file marker."
            };
        }

        const std::uint32_t version =
            readScalar<std::uint32_t>(input);

        if (
            version < oldestSupportedFormatVersion
            || version > formatVersion)
        {
            throw std::runtime_error{
                "Rose workspace metadata uses an unsupported format version."
            };
        }

        WorkspaceSnapshot snapshot;

        const std::uint32_t projectCount =
            readScalar<std::uint32_t>(input);

        if (projectCount > maximumProjects)
        {
            throw std::runtime_error{
                "Rose workspace metadata contains too many projects."
            };
        }

        snapshot.projects.reserve(projectCount);

        for (std::uint32_t index = 0; index < projectCount; ++index)
        {
            ProjectRecord project;
            project.id = readString(input);
            project.title = readString(input);
            project.instructions = readString(input);
            project.approvedFilesystemRoots = readStringList(input);
            project.indexedDocuments = readStringList(input);
            project.memoryRecordIds = readUint64List(input);
            project.enabledTools = readStringList(input);
            project.settings = readSettings(input);
            project.createdUnixMilliseconds = readScalar<std::int64_t>(input);
            project.updatedUnixMilliseconds = readScalar<std::int64_t>(input);

            if (version >= 2)
            {
                const std::uint8_t removed = readScalar<std::uint8_t>(input);
                if (removed > 1)
                {
                    throw std::runtime_error{
                        "Rose workspace metadata contains an invalid project removal flag."
                    };
                }
                project.removed = removed == 1;
            }

            snapshot.projects.push_back(std::move(project));
        }

        const std::uint32_t discussionCount =
            readScalar<std::uint32_t>(input);

        if (discussionCount > maximumDiscussions)
        {
            throw std::runtime_error{
                "Rose workspace metadata contains too many discussions."
            };
        }

        snapshot.discussions.reserve(discussionCount);

        for (std::uint32_t index = 0; index < discussionCount; ++index)
        {
            DiscussionRecord discussion;
            discussion.id = readString(input);
            discussion.title = readString(input);

            const std::uint8_t hasProject =
                readScalar<std::uint8_t>(input);

            if (hasProject > 1)
            {
                throw std::runtime_error{
                    "Rose workspace metadata contains an invalid optional project flag."
                };
            }

            if (hasProject == 1)
            {
                discussion.projectId = readString(input);
            }

            discussion.tags = readStringList(input);
            discussion.createdUnixMilliseconds = readScalar<std::int64_t>(input);
            discussion.updatedUnixMilliseconds = readScalar<std::int64_t>(input);

            if (version >= 2)
            {
                const std::uint8_t removed = readScalar<std::uint8_t>(input);
                if (removed > 1)
                {
                    throw std::runtime_error{
                        "Rose workspace metadata contains an invalid discussion removal flag."
                    };
                }
                discussion.removed = removed == 1;
            }

            snapshot.discussions.push_back(std::move(discussion));
        }

        snapshot.openDiscussionIds = readStringList(input);

        const std::uint8_t hasActive =
            readScalar<std::uint8_t>(input);

        if (hasActive > 1)
        {
            throw std::runtime_error{
                "Rose workspace metadata contains an invalid active-discussion flag."
            };
        }

        if (hasActive == 1)
        {
            snapshot.activeDiscussionId = readString(input);
        }

        return snapshot;
    }


    void FileWorkspaceStore::save(
        const WorkspaceSnapshot& snapshot)
    {
        ensureParentDirectory(path_);

        std::filesystem::path temporary = path_;
        temporary += ".tmp";

        // Recreate the temporary file from scratch. A previous crash may have
        // left a stale .tmp file, which is safe to discard here.
        std::ofstream output{
            temporary,
            std::ios::binary | std::ios::trunc
        };

        if (!output)
        {
            throw std::runtime_error{
                std::string{
                    "Could not open temporary Rose workspace metadata: "
                }
                + temporary.string()
            };
        }

        output.write(
            fileMagic.data(),
            static_cast<std::streamsize>(fileMagic.size()));

        if (!output)
        {
            throw std::runtime_error{
                "Could not write Rose workspace file marker."
            };
        }

        writeScalar(output, formatVersion);

        writeScalar(
            output,
            checkedSize(
                snapshot.projects.size(),
                maximumProjects,
                "Project count"));

        for (const ProjectRecord& project : snapshot.projects)
        {
            writeString(output, project.id);
            writeString(output, project.title);
            writeString(output, project.instructions);
            writeStringList(output, project.approvedFilesystemRoots);
            writeStringList(output, project.indexedDocuments);
            writeUint64List(output, project.memoryRecordIds);
            writeStringList(output, project.enabledTools);
            writeSettings(output, project.settings);
            writeScalar(output, project.createdUnixMilliseconds);
            writeScalar(output, project.updatedUnixMilliseconds);
            writeScalar<std::uint8_t>(output, project.removed ? 1 : 0);
        }

        writeScalar(
            output,
            checkedSize(
                snapshot.discussions.size(),
                maximumDiscussions,
                "Discussion count"));

        for (const DiscussionRecord& discussion : snapshot.discussions)
        {
            writeString(output, discussion.id);
            writeString(output, discussion.title);

            writeScalar<std::uint8_t>(
                output,
                discussion.projectId.has_value() ? 1 : 0);

            if (discussion.projectId.has_value())
            {
                writeString(output, *discussion.projectId);
            }

            writeStringList(output, discussion.tags);
            writeScalar(output, discussion.createdUnixMilliseconds);
            writeScalar(output, discussion.updatedUnixMilliseconds);
            writeScalar<std::uint8_t>(output, discussion.removed ? 1 : 0);
        }

        writeStringList(output, snapshot.openDiscussionIds);

        writeScalar<std::uint8_t>(
            output,
            snapshot.activeDiscussionId.has_value() ? 1 : 0);

        if (snapshot.activeDiscussionId.has_value())
        {
            writeString(output, *snapshot.activeDiscussionId);
        }

        output.flush();

        if (!output)
        {
            throw std::runtime_error{
                "Could not flush Rose workspace metadata."
            };
        }

        output.close();

        if (!output)
        {
            throw std::runtime_error{
                "Could not close Rose workspace metadata cleanly."
            };
        }

        replaceFile(temporary, path_);
    }


    const std::filesystem::path&
        FileWorkspaceStore::path() const noexcept
    {
        return path_;
    }

} // namespace rose::workspace
