#pragma once

#include "jobs/JobTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rose::jobs
{
    enum class ErrandCommandKind
    {
        List,
        ScheduleReminder,
        Cancel
    };

    struct ErrandCommand
    {
        ErrandCommandKind kind{ ErrandCommandKind::List };
        std::int64_t delayMilliseconds{ 0 };
        std::string text;
        std::string jobId;
    };

    // Returns nullopt when text is not an errand command. Recognized but malformed
    // commands throw invalid_argument so the UI can return local usage help.
    [[nodiscard]] std::optional<ErrandCommand>
        parseErrandCommand(std::string_view text);

    [[nodiscard]] std::string jobStatusName(JobStatus status);
}
