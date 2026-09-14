#include "persistence/FileConversationStore.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace rose::persistence
{

    namespace
    {

        // ---------------------------------------------------------------------
        // Journal format
        // ---------------------------------------------------------------------
        //
        // Every complete record is:
        //
        //     magic
        //     version
        //     timestamp
        //     user byte count
        //     assistant byte count
        //     user UTF-8 bytes
        //     assistant UTF-8 bytes
        //
        // A complete user/assistant turn lives in one record.
        //
        // If Rose crashes while the final record is being written, loadTurns()
        // simply ignores that incomplete trailing record.

        constexpr std::array<char, 8> recordMagic{
            'R', 'O', 'S', 'E',
            'T', 'R', 'N', '1'
        };


        constexpr std::uint32_t formatVersion{
            1
        };


        // Defensive corruption/accidental-allocation limit.
        //
        // This is intentionally much larger than anything Rose should currently
        // put into one normal conversation message.
        constexpr std::uint32_t maximumTextBytes{
            16U * 1024U * 1024U
        };


        template<typename T>
        void appendScalar(
            std::string& destination,
            const T value)
        {
            static_assert(
                std::is_trivially_copyable_v<T>);


            const auto* bytes =
                reinterpret_cast<const char*>(
                    &value);


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
                reinterpret_cast<char*>(
                    &value),
                sizeof(T));


            return
                input.gcount()
                == static_cast<std::streamsize>(
                    sizeof(T));
        }


        [[nodiscard]]
        std::int64_t currentUnixMilliseconds()
        {
            const auto now =
                std::chrono::system_clock::now();


            return
                std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now.time_since_epoch())
                .count();
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
                    std::string{
                        "Could not create Rose persistence directory: "
                    }
                    + error.message()
                };
            }
        }


        [[nodiscard]]
        std::uint32_t checkedLength(
            const std::string_view text)
        {
            if (
                text.size()
                > maximumTextBytes)
            {
                throw std::runtime_error{
                    "Conversation message exceeds Rose's persistence record limit."
                };
            }


            return
                static_cast<std::uint32_t>(
                    text.size());
        }

    } // namespace


    FileConversationStore::FileConversationStore(
        std::filesystem::path path)
        : path_{
            std::move(path)
        }
    {
        if (path_.empty())
        {
            throw std::invalid_argument{
                "FileConversationStore requires a path."
            };
        }
    }


    std::vector<StoredConversationTurn>
        FileConversationStore::loadTurns()
    {
        std::vector<StoredConversationTurn> turns;


        // First launch is not an error.
        if (!std::filesystem::exists(path_))
        {
            return turns;
        }


        std::ifstream input{
            path_,
            std::ios::binary
        };


        if (!input)
        {
            throw std::runtime_error{
                std::string{
                    "Could not open Rose conversation journal: "
                }
                + path_.string()
            };
        }


        while (true)
        {
            std::array<char, recordMagic.size()>
                magic{};


            input.read(
                magic.data(),
                static_cast<std::streamsize>(
                    magic.size()));


            const std::streamsize magicBytesRead =
                input.gcount();


            // Clean EOF.
            if (magicBytesRead == 0)
            {
                break;
            }


            // Rose may have crashed while writing the final record.
            //
            // An incomplete trailing record is ignored rather than making the
            // entire historical conversation unreadable.
            if (
                magicBytesRead
                != static_cast<std::streamsize>(
                    magic.size()))
            {
                break;
            }


            if (magic != recordMagic)
            {
                throw std::runtime_error{
                    "Rose conversation journal contains an invalid record marker."
                };
            }


            std::uint32_t version{ 0 };

            std::int64_t timestamp{ 0 };

            std::uint32_t userBytes{ 0 };
            std::uint32_t assistantBytes{ 0 };


            if (
                !readScalar(
                    input,
                    version)
                || !readScalar(
                    input,
                    timestamp)
                || !readScalar(
                    input,
                    userBytes)
                || !readScalar(
                    input,
                    assistantBytes))
            {
                // Partial trailing header.
                break;
            }


            if (version != formatVersion)
            {
                throw std::runtime_error{
                    "Rose conversation journal uses an unsupported format version."
                };
            }


            if (
                userBytes > maximumTextBytes
                || assistantBytes > maximumTextBytes)
            {
                throw std::runtime_error{
                    "Rose conversation journal contains an invalid record size."
                };
            }


            StoredConversationTurn turn;

            turn.unixTimeMilliseconds =
                timestamp;


            turn.userText.resize(
                userBytes);


            turn.assistantText.resize(
                assistantBytes);


            if (userBytes > 0)
            {
                input.read(
                    turn.userText.data(),
                    static_cast<std::streamsize>(
                        userBytes));


                if (
                    input.gcount()
                    != static_cast<std::streamsize>(
                        userBytes))
                {
                    // Partial trailing payload.
                    break;
                }
            }


            if (assistantBytes > 0)
            {
                input.read(
                    turn.assistantText.data(),
                    static_cast<std::streamsize>(
                        assistantBytes));


                if (
                    input.gcount()
                    != static_cast<std::streamsize>(
                        assistantBytes))
                {
                    // Partial trailing payload.
                    break;
                }
            }


            turns.push_back(
                std::move(
                    turn));
        }


        return turns;
    }


    void FileConversationStore::appendTurn(
        const std::string_view userText,
        const std::string_view assistantText)
    {
        const std::uint32_t userBytes =
            checkedLength(
                userText);


        const std::uint32_t assistantBytes =
            checkedLength(
                assistantText);


        ensureParentDirectory(
            path_);


        // Assemble the complete record in memory first.
        //
        // That gives the filesystem one contiguous write rather than slowly
        // constructing the record directly in the journal.
        std::string record;


        constexpr std::size_t headerBytes =
            recordMagic.size()
            + sizeof(std::uint32_t)
            + sizeof(std::int64_t)
            + sizeof(std::uint32_t)
            + sizeof(std::uint32_t);


        record.reserve(
            headerBytes
            + userText.size()
            + assistantText.size());


        record.append(
            recordMagic.data(),
            recordMagic.size());


        appendScalar(
            record,
            formatVersion);


        appendScalar(
            record,
            currentUnixMilliseconds());


        appendScalar(
            record,
            userBytes);


        appendScalar(
            record,
            assistantBytes);


        record.append(
            userText.data(),
            userText.size());


        record.append(
            assistantText.data(),
            assistantText.size());


        std::ofstream output{
            path_,
            std::ios::binary
            | std::ios::app
        };


        if (!output)
        {
            throw std::runtime_error{
                std::string{
                    "Could not open Rose conversation journal for writing: "
                }
                + path_.string()
            };
        }


        output.write(
            record.data(),
            static_cast<std::streamsize>(
                record.size()));


        if (!output)
        {
            throw std::runtime_error{
                "Could not write Rose conversation journal."
            };
        }


        // Flush every successful turn.
        //
        // This is slightly more expensive than batching writes, but conversation
        // turns occur vastly less frequently than rendering or model tokens and
        // crash recovery matters more here than throughput.
        output.flush();


        if (!output)
        {
            throw std::runtime_error{
                "Could not flush Rose conversation journal."
            };
        }
    }


    void FileConversationStore::clear()
    {
        std::error_code error;


        std::filesystem::remove(
            path_,
            error);


        if (error)
        {
            throw std::runtime_error{
                std::string{
                    "Could not clear Rose conversation journal: "
                }
                + error.message()
            };
        }
    }


    const std::filesystem::path&
        FileConversationStore::path() const noexcept
    {
        return path_;
    }

} // namespace rose::persistence