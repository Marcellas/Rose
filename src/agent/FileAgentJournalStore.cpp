#include "agent/FileAgentJournalStore.h"

#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace rose::agent
{
    namespace
    {
        constexpr std::string_view header{ "ROSE_AGENT_JOURNAL_V1" };
        constexpr std::size_t maximumEventsOnDisk{ 4096 };
        constexpr std::size_t maximumStringBytes{ 1024 * 1024 };

        [[nodiscard]]
        std::string readLine(
            std::istream& input,
            const std::string_view what)
        {
            std::string value;
            if (!std::getline(input, value))
            {
                throw std::runtime_error{
                    "Agent journal ended while reading "
                    + std::string{ what }
                    + "."
                };
            }
            return value;
        }

        template <typename Integer>
        [[nodiscard]]
        Integer parseInteger(
            const std::string_view text,
            const std::string_view what)
        {
            Integer value{};
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            const auto result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{} || result.ptr != end)
            {
                throw std::runtime_error{
                    "Agent journal contains an invalid "
                    + std::string{ what }
                    + "."
                };
            }
            return value;
        }

        [[nodiscard]]
        std::size_t readCount(
            std::istream& input,
            const std::size_t maximum,
            const std::string_view what)
        {
            const auto value = parseInteger<unsigned long long>(
                readLine(input, what),
                what);
            if (value > maximum)
            {
                throw std::runtime_error{
                    "Agent journal "
                    + std::string{ what }
                    + " exceeds Rose's safety limit."
                };
            }
            return static_cast<std::size_t>(value);
        }

        [[nodiscard]]
        std::string readString(
            std::istream& input,
            const std::string_view what)
        {
            const std::size_t size = readCount(
                input,
                maximumStringBytes,
                what);

            std::string value(size, '\0');
            if (size > 0)
            {
                input.read(
                    value.data(),
                    static_cast<std::streamsize>(size));
                if (input.gcount() != static_cast<std::streamsize>(size))
                {
                    throw std::runtime_error{
                        "Agent journal ended inside "
                        + std::string{ what }
                        + "."
                    };
                }
            }

            if (input.get() != '\n')
            {
                throw std::runtime_error{
                    "Agent journal has an invalid string delimiter."
                };
            }
            return value;
        }

        void writeString(
            std::ostream& output,
            const std::string_view value)
        {
            output << value.size() << '\n';
            output.write(
                value.data(),
                static_cast<std::streamsize>(value.size()));
            output << '\n';
        }
    }

    FileAgentJournalStore::FileAgentJournalStore(
        std::filesystem::path path)
        : path_{ std::move(path) }
    {
        if (path_.empty())
        {
            throw std::invalid_argument{
                "Agent journal store path cannot be empty."
            };
        }
    }

    std::vector<AgentEvent> FileAgentJournalStore::load()
    {
        std::error_code error;
        if (!std::filesystem::exists(path_, error))
        {
            if (error)
            {
                throw std::runtime_error{
                    "Could not inspect agent journal: " + error.message()
                };
            }
            return {};
        }

        std::ifstream input{ path_, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{
                "Could not open agent journal: " + path_.string()
            };
        }

        if (readLine(input, "header") != header)
        {
            throw std::runtime_error{
                "Agent journal has an unsupported format."
            };
        }

        const std::size_t count = readCount(
            input,
            maximumEventsOnDisk,
            "event count");

        std::vector<AgentEvent> events;
        events.reserve(count);

        for (std::size_t index = 0; index < count; ++index)
        {
            AgentEvent event;
            event.sequence = parseInteger<std::uint64_t>(
                readLine(input, "sequence"),
                "sequence");
            event.runId = parseInteger<std::uint64_t>(
                readLine(input, "run id"),
                "run id");

            const auto typeValue = parseInteger<unsigned int>(
                readLine(input, "event type"),
                "event type");
            if (typeValue > static_cast<unsigned int>(AgentEventType::RepairValidationFailed))
            {
                throw std::runtime_error{
                    "Agent journal contains an unknown event type."
                };
            }
            event.type = static_cast<AgentEventType>(typeValue);

            const auto timestampMs = parseInteger<std::int64_t>(
                readLine(input, "timestamp"),
                "timestamp");
            event.timestamp = std::chrono::system_clock::time_point{
                std::chrono::milliseconds{ timestampMs }
            };

            event.stepIndex = readCount(
                input,
                (std::numeric_limits<std::size_t>::max)(),
                "step index");
            event.duration = std::chrono::milliseconds{
                parseInteger<std::int64_t>(
                    readLine(input, "duration"),
                    "duration")
            };
            event.toolId = readString(input, "tool id");
            event.projectId = readString(input, "project id");
            event.discussionId = readString(input, "discussion id");
            event.message = readString(input, "message");
            event.detail = readString(input, "detail");
            events.push_back(std::move(event));
        }

        return events;
    }

    void FileAgentJournalStore::save(
        const std::vector<AgentEvent>& events)
    {
        std::error_code error;
        if (path_.has_parent_path())
        {
            std::filesystem::create_directories(
                path_.parent_path(),
                error);
            if (error)
            {
                throw std::runtime_error{
                    "Could not create agent journal directory: "
                    + error.message()
                };
            }
        }

        std::filesystem::path temporary = path_;
        temporary += ".tmp";

        {
            std::ofstream output{
                temporary,
                std::ios::binary | std::ios::trunc
            };
            if (!output)
            {
                throw std::runtime_error{
                    "Could not create temporary agent journal: "
                    + temporary.string()
                };
            }

            output << header << '\n';
            output << events.size() << '\n';

            for (const AgentEvent& event : events)
            {
                const auto timestampMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        event.timestamp.time_since_epoch()).count();

                output << event.sequence << '\n';
                output << event.runId << '\n';
                output << static_cast<unsigned int>(event.type) << '\n';
                output << timestampMs << '\n';
                output << event.stepIndex << '\n';
                output << event.duration.count() << '\n';
                writeString(output, event.toolId);
                writeString(output, event.projectId);
                writeString(output, event.discussionId);
                writeString(output, event.message);
                writeString(output, event.detail);
            }

            output.flush();
            if (!output)
            {
                throw std::runtime_error{
                    "Could not finish writing agent journal."
                };
            }
        }

        std::filesystem::remove(path_, error);
        error.clear();
        std::filesystem::rename(temporary, path_, error);
        if (error)
        {
            std::filesystem::remove(temporary);
            throw std::runtime_error{
                "Could not publish agent journal: " + error.message()
            };
        }
    }

    const std::filesystem::path&
        FileAgentJournalStore::path() const noexcept
    {
        return path_;
    }
}
