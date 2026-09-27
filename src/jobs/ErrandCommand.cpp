#include "jobs/ErrandCommand.h"

#include <charconv>
#include <limits>
#include <stdexcept>

namespace rose::jobs
{
    namespace
    {
        constexpr std::string_view schedulePrefix{ "/errand in " };
        constexpr std::string_view cancelPrefix{ "/errand cancel " };

        std::int64_t multiplierFor(const char unit)
        {
            switch (unit)
            {
            case 's': return 1'000;
            case 'm': return 60'000;
            case 'h': return 3'600'000;
            case 'd': return 86'400'000;
            default:
                throw std::invalid_argument{
                    "Reminder delay unit must be s, m, h, or d. Example: /errand in 10m Stretch."
                };
            }
        }
    }

    std::optional<ErrandCommand> parseErrandCommand(const std::string_view text)
    {
        if (text == "/errands")
        {
            return ErrandCommand{
                .kind = ErrandCommandKind::List,
                .delayMilliseconds = 0,
                .text = {},
                .jobId = {}
            };
        }

        if (text.starts_with(cancelPrefix))
        {
            const std::string_view id = text.substr(cancelPrefix.size());
            if (id.empty())
            {
                throw std::invalid_argument{ "Usage: /errand cancel <job-id>" };
            }
            return ErrandCommand{
                .kind = ErrandCommandKind::Cancel,
                .delayMilliseconds = 0,
                .text = {},
                .jobId = std::string{ id }
            };
        }

        if (text.starts_with(schedulePrefix))
        {
            std::string_view remainder = text.substr(schedulePrefix.size());
            const std::size_t space = remainder.find(' ');
            if (space == std::string_view::npos || space == 0 || space + 1 >= remainder.size())
            {
                throw std::invalid_argument{
                    "Usage: /errand in <delay> <reminder>, for example /errand in 10m Stretch."
                };
            }

            const std::string_view delayToken = remainder.substr(0, space);
            if (delayToken.size() < 2)
            {
                throw std::invalid_argument{ "Reminder delay must look like 30s, 10m, 2h, or 1d." };
            }

            const char unit = delayToken.back();
            const std::string_view amountText = delayToken.substr(0, delayToken.size() - 1);

            std::int64_t amount{ 0 };
            const auto parseResult = std::from_chars(
                amountText.data(),
                amountText.data() + amountText.size(),
                amount);

            if (
                parseResult.ec != std::errc{}
                || parseResult.ptr != amountText.data() + amountText.size()
                || amount <= 0)
            {
                throw std::invalid_argument{ "Reminder delay must use a positive whole number." };
            }

            const std::int64_t multiplier = multiplierFor(unit);
            if (amount > std::numeric_limits<std::int64_t>::max() / multiplier)
            {
                throw std::invalid_argument{ "Reminder delay is too large." };
            }

            return ErrandCommand{
                .kind = ErrandCommandKind::ScheduleReminder,
                .delayMilliseconds = amount * multiplier,
                .text = std::string{ remainder.substr(space + 1) },
                .jobId = {}
            };
        }

        if (text.starts_with("/errands"))
        {
            throw std::invalid_argument{ "Usage: /errands" };
        }

        if (text == "/errand" || text.starts_with("/errand "))
        {
            throw std::invalid_argument{
                "Usage: /errand in <delay> <reminder>, /errands, or /errand cancel <job-id>."
            };
        }

        return std::nullopt;
    }

    std::string jobStatusName(const JobStatus status)
    {
        switch (status)
        {
        case JobStatus::Pending: return "pending";
        case JobStatus::Running: return "running";
        case JobStatus::Completed: return "completed";
        case JobStatus::Cancelled: return "cancelled";
        case JobStatus::Failed: return "failed";
        }
        return "unknown";
    }
}
