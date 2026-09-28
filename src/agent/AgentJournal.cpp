#include "agent/AgentJournal.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace rose::agent
{
    namespace
    {
        [[nodiscard]]
        std::string escapeSingleLine(
            const std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            for (const char value : text)
            {
                switch (value)
                {
                case '\r':
                    result += "\\r";
                    break;

                case '\n':
                    result += "\\n";
                    break;

                case '\t':
                    result += "\\t";
                    break;

                default:
                    result.push_back(value);
                    break;
                }
            }

            return result;
        }


        [[nodiscard]]
        std::string lowerCopy(
            const std::string_view text)
        {
            std::string result{ text };

            std::transform(
                result.begin(),
                result.end(),
                result.begin(),
                [](const unsigned char value)
                {
                    return static_cast<char>(
                        std::tolower(value));
                });

            return result;
        }


        [[nodiscard]]
        bool sensitiveArgumentName(
            const std::string_view name)
        {
            const std::string lower =
                lowerCopy(name);

            constexpr std::string_view sensitiveTerms[]{
                "password",
                "passwd",
                "secret",
                "token",
                "credential",
                "api_key",
                "apikey",
                "authorization"
            };

            for (const std::string_view term : sensitiveTerms)
            {
                if (lower.find(term) != std::string::npos)
                {
                    return true;
                }
            }

            return false;
        }


        [[nodiscard]]
        std::tm localTime(
            const std::time_t value)
        {
            std::tm result{};

#ifdef _WIN32
            localtime_s(
                &result,
                &value);
#else
            localtime_r(
                &value,
                &result);
#endif

            return result;
        }
    }


    const char* agentEventTypeName(
        const AgentEventType type) noexcept
    {
        switch (type)
        {
        case AgentEventType::RunStarted:
            return "RunStarted";

        case AgentEventType::DecisionMade:
            return "DecisionMade";

        case AgentEventType::ToolProposed:
            return "ToolProposed";

        case AgentEventType::ConfirmationRequired:
            return "ConfirmationRequired";

        case AgentEventType::ConfirmationGranted:
            return "ConfirmationGranted";

        case AgentEventType::ConfirmationDenied:
            return "ConfirmationDenied";

        case AgentEventType::PolicyDenied:
            return "PolicyDenied";

        case AgentEventType::ToolStarted:
            return "ToolStarted";

        case AgentEventType::ToolFinished:
            return "ToolFinished";

        case AgentEventType::ToolFailed:
            return "ToolFailed";

        case AgentEventType::DuplicateActionBlocked:
            return "DuplicateActionBlocked";

        case AgentEventType::StepLimitReached:
            return "StepLimitReached";

        case AgentEventType::RunCompleted:
            return "RunCompleted";

        case AgentEventType::RunFailed:
            return "RunFailed";

        case AgentEventType::OperationStarted:
            return "OperationStarted";

        case AgentEventType::OperationFinished:
            return "OperationFinished";

        case AgentEventType::OperationFailed:
            return "OperationFailed";

        case AgentEventType::RepairPlanned:
            return "RepairPlanned";

        case AgentEventType::RepairApplied:
            return "RepairApplied";

        case AgentEventType::RepairValidated:
            return "RepairValidated";

        case AgentEventType::RepairValidationFailed:
            return "RepairValidationFailed";
        }

        return "Unknown";
    }


    AgentJournal::AgentJournal(
        AgentJournalConfig config,
        IAgentJournalStore* store)
        : config_{ config }
        , store_{ store }
    {
        if (config_.maximumEvents == 0)
        {
            throw std::invalid_argument{
                "AgentJournal maximumEvents must be greater than zero."
            };
        }

        if (
            config_.maximumMessageBytes == 0
            || config_.maximumDetailBytes == 0
            || config_.maximumArgumentValueBytes == 0)
        {
            throw std::invalid_argument{
                "AgentJournal text limits must be greater than zero."
            };
        }

        events_.reserve(
            config_.maximumEvents);

        restoreBestEffort();
    }


    std::uint64_t AgentJournal::beginRun(
        const std::string_view userRequest,
        AgentRunProvenance provenance)
    {
        const std::uint64_t runId =
            nextRunId_++;

        runProvenance_.insert_or_assign(
            runId,
            provenance);

        record(
            AgentEvent{
                .runId = runId,
                .type = AgentEventType::RunStarted,
                .stepIndex = 0,
                .toolId = {},
                .projectId = provenance.projectId,
                .discussionId = provenance.discussionId,
                .message = "Started bounded agent run.",
                .detail =
                    "user_request_bytes="
                    + std::to_string(userRequest.size())
            });

        return runId;
    }


    void AgentJournal::record(
        AgentEvent event)
    {
        event.sequence =
            nextSequence_++;

        if (
            event.timestamp
            == std::chrono::system_clock::time_point{})
        {
            event.timestamp =
                std::chrono::system_clock::now();
        }

        if (event.runId != 0)
        {
            const auto provenance =
                runProvenance_.find(event.runId);

            if (provenance != runProvenance_.end())
            {
                if (event.projectId.empty())
                {
                    event.projectId = provenance->second.projectId;
                }
                if (event.discussionId.empty())
                {
                    event.discussionId = provenance->second.discussionId;
                }
            }
        }

        event.toolId =
            boundText(
                event.toolId,
                config_.maximumMessageBytes);

        event.projectId =
            boundText(
                event.projectId,
                config_.maximumMessageBytes);

        event.discussionId =
            boundText(
                event.discussionId,
                config_.maximumMessageBytes);

        event.message =
            boundText(
                event.message,
                config_.maximumMessageBytes);

        event.detail =
            boundText(
                event.detail,
                config_.maximumDetailBytes);

        const std::uint64_t completedRunId =
            (event.type == AgentEventType::RunCompleted
             || event.type == AgentEventType::RunFailed)
                ? event.runId
                : 0;

        if (
            events_.size()
            < config_.maximumEvents)
        {
            events_.push_back(
                std::move(event));

            if (
                events_.size()
                == config_.maximumEvents)
            {
                nextWriteIndex_ = 0;
            }
        }
        else
        {
            events_[nextWriteIndex_] =
                std::move(event);

            nextWriteIndex_ =
                (nextWriteIndex_ + 1)
                % config_.maximumEvents;
        }

        if (completedRunId != 0)
        {
            runProvenance_.erase(completedRunId);
        }

        persistBestEffort();
    }


    void AgentJournal::recordToolRequest(
        const std::uint64_t runId,
        const AgentEventType type,
        const std::size_t stepIndex,
        const tools::ToolRequest& request,
        const std::string_view message,
        const std::chrono::milliseconds duration)
    {
        record(
            AgentEvent{
                .runId = runId,
                .type = type,
                .stepIndex = stepIndex,
                .toolId = request.toolId,
                .projectId = {},
                .discussionId = {},
                .message = std::string{ message },
                .detail = formatToolRequest(request),
                .duration = duration
            });
    }


    std::vector<AgentEvent> AgentJournal::snapshot() const
    {
        std::vector<AgentEvent> result;
        result.reserve(events_.size());

        if (
            events_.size()
            < config_.maximumEvents)
        {
            result = events_;
            return result;
        }

        // When full, nextWriteIndex_ points at the oldest event: the slot that will
        // be overwritten on the next record(). Return logical chronological order.
        for (
            std::size_t offset = 0;
            offset < events_.size();
            ++offset)
        {
            const std::size_t index =
                (nextWriteIndex_ + offset)
                % events_.size();

            result.push_back(
                events_[index]);
        }

        return result;
    }


    std::string AgentJournal::formatRecent(
        const std::size_t maximumEvents) const
    {
        const std::vector<AgentEvent> copy =
            snapshot();

        if (copy.empty())
        {
            return "Agent journal is empty.";
        }

        const std::size_t count =
            maximumEvents == 0
                ? copy.size()
                : (std::min)(
                    maximumEvents,
                    copy.size());

        const std::size_t first =
            copy.size() - count;

        std::ostringstream text;
        text
            << "Agent journal: showing "
            << count
            << " of "
            << copy.size()
            << " retained event(s).\n";

        if (!persistenceError_.empty())
        {
            text
                << "Persistence warning: "
                << persistenceError_
                << "\n";
        }

        for (
            std::size_t index = first;
            index < copy.size();
            ++index)
        {
            const AgentEvent& event =
                copy[index];

            const std::time_t wallTime =
                std::chrono::system_clock::to_time_t(
                    event.timestamp);

            const std::tm time =
                localTime(wallTime);

            text
                << '#'
                << event.sequence
                << " ["
                << std::put_time(
                    &time,
                    "%H:%M:%S")
                << "] run="
                << event.runId
                << ' '
                << agentEventTypeName(event.type);

            if (event.stepIndex != 0)
            {
                text
                    << " step="
                    << event.stepIndex;
            }

            if (!event.toolId.empty())
            {
                text
                    << " tool="
                    << event.toolId;
            }

            if (!event.projectId.empty())
            {
                text
                    << " project="
                    << event.projectId;
            }

            if (!event.discussionId.empty())
            {
                text
                    << " discussion="
                    << event.discussionId;
            }

            if (event.duration.count() != 0)
            {
                text
                    << " duration_ms="
                    << event.duration.count();
            }

            if (!event.message.empty())
            {
                text
                    << "\n  "
                    << event.message;
            }

            if (!event.detail.empty())
            {
                text
                    << "\n  "
                    << event.detail;
            }

            text << '\n';
        }

        return text.str();
    }


    void AgentJournal::clear() noexcept
    {
        events_.clear();
        nextWriteIndex_ = 0;
        runProvenance_.clear();
        persistBestEffort();
    }


    std::size_t AgentJournal::size() const noexcept
    {
        return events_.size();
    }


    std::size_t AgentJournal::capacity() const noexcept
    {
        return config_.maximumEvents;
    }


    const std::string& AgentJournal::persistenceError() const noexcept
    {
        return persistenceError_;
    }


    void AgentJournal::restoreBestEffort() noexcept
    {
        if (store_ == nullptr)
        {
            return;
        }

        try
        {
            std::vector<AgentEvent> loaded = store_->load();

            if (loaded.size() > config_.maximumEvents)
            {
                loaded.erase(
                    loaded.begin(),
                    loaded.end() - static_cast<std::ptrdiff_t>(config_.maximumEvents));
            }

            events_ = std::move(loaded);
            nextWriteIndex_ =
                events_.size() == config_.maximumEvents
                    ? 0
                    : events_.size();

            std::uint64_t maximumSequence{ 0 };
            std::uint64_t maximumRunId{ 0 };
            for (const AgentEvent& event : events_)
            {
                maximumSequence = (std::max)(maximumSequence, event.sequence);
                maximumRunId = (std::max)(maximumRunId, event.runId);
            }

            nextSequence_ = maximumSequence + 1;
            nextRunId_ = maximumRunId + 1;
            persistenceError_.clear();
        }
        catch (const std::exception& exception)
        {
            events_.clear();
            nextWriteIndex_ = 0;
            nextSequence_ = 1;
            nextRunId_ = 1;
            persistenceError_ = exception.what();
        }
        catch (...)
        {
            events_.clear();
            nextWriteIndex_ = 0;
            nextSequence_ = 1;
            nextRunId_ = 1;
            persistenceError_ =
                "Agent journal persistence failed with an unknown exception.";
        }
    }


    void AgentJournal::persistBestEffort() noexcept
    {
        if (store_ == nullptr)
        {
            return;
        }

        try
        {
            store_->save(snapshot());
            persistenceError_.clear();
        }
        catch (const std::exception& exception)
        {
            persistenceError_ = exception.what();
        }
        catch (...)
        {
            persistenceError_ =
                "Agent journal persistence failed with an unknown exception.";
        }
    }


    std::string AgentJournal::boundText(
        const std::string_view text,
        const std::size_t maximumBytes) const
    {
        if (text.size() <= maximumBytes)
        {
            return std::string{ text };
        }

        constexpr std::string_view suffix{
            "...[truncated]"
        };

        // For extremely small limits, prefer a valid ASCII truncation marker over
        // slicing an arbitrary UTF-8 code point from the source text.
        if (maximumBytes <= suffix.size())
        {
            return std::string{
                suffix.substr(
                    0,
                    maximumBytes)
            };
        }

        std::size_t prefixBytes =
            maximumBytes - suffix.size();

        // Do not cut in the middle of a UTF-8 continuation sequence. Rose treats
        // text as UTF-8 throughout the UI, so a bounded journal should preserve
        // that invariant even when diagnostics are truncated.
        while (
            prefixBytes > 0
            && prefixBytes < text.size()
            && (
                static_cast<unsigned char>(text[prefixBytes])
                & 0xC0u)
                == 0x80u)
        {
            --prefixBytes;
        }

        std::string result{
            text.substr(
                0,
                prefixBytes)
        };

        result += suffix;
        return result;
    }


    std::string AgentJournal::formatToolRequest(
        const tools::ToolRequest& request) const
    {
        std::vector<std::pair<std::string, std::string>> arguments;
        arguments.reserve(request.arguments.size());

        for (const auto& [name, value] : request.arguments)
        {
            arguments.emplace_back(
                name,
                value);
        }

        std::sort(
            arguments.begin(),
            arguments.end(),
            [](const auto& left, const auto& right)
            {
                return left.first < right.first;
            });

        std::ostringstream text;
        text
            << "request="
            << request.toolId;

        for (const auto& [name, value] : arguments)
        {
            const std::string boundedValue =
                sensitiveArgumentName(name)
                    ? std::string{ "<redacted>" }
                    : boundText(
                        escapeSingleLine(value),
                        config_.maximumArgumentValueBytes);

            text
                << " | "
                << name
                << '='
                << boundedValue;
        }

        return boundText(
            text.str(),
            config_.maximumDetailBytes);
    }

} // namespace rose::agent
