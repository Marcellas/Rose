#pragma once

#include "tools/ToolTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rose::agent
{

    // Structured events emitted by Rose's bounded Agent execution path.
    //
    // These are intentionally higher-level than the ordinary Logger stream. They
    // describe agent decisions, permission boundaries, and real tool execution so
    // Rose can later explain what happened without scraping human-readable logs.
    enum class AgentEventType
    {
        RunStarted,
        DecisionMade,
        ToolProposed,
        ConfirmationRequired,
        ConfirmationGranted,
        ConfirmationDenied,
        PolicyDenied,
        ToolStarted,
        ToolFinished,
        ToolFailed,
        DuplicateActionBlocked,
        StepLimitReached,
        RunCompleted,
        RunFailed
    };


    struct AgentEvent
    {
        std::uint64_t sequence{ 0 };
        std::uint64_t runId{ 0 };

        AgentEventType type{
            AgentEventType::RunStarted
        };

        std::chrono::system_clock::time_point timestamp{};

        // 1-based tool step where meaningful. Zero means the event is not tied to
        // one concrete tool step.
        std::size_t stepIndex{ 0 };

        // Empty for events that are not associated with a tool.
        std::string toolId;

        // Short human-readable explanation. Kept bounded by AgentJournal.
        std::string message;

        // Additional structured-ish diagnostic text. For tool requests this may
        // contain a deterministic, bounded argument summary.
        std::string detail;

        // Populated for ToolFinished / ToolFailed timing measurements.
        std::chrono::milliseconds duration{ 0 };
    };


    struct AgentJournalConfig
    {
        // Fixed event capacity. Once full, the oldest event is overwritten.
        std::size_t maximumEvents{ 256 };

        // Individual text fields are bounded so a giant prompt or file body cannot
        // turn the journal into an unbounded hidden memory store.
        std::size_t maximumMessageBytes{ 512 };
        std::size_t maximumDetailBytes{ 2048 };

        // Each individual argument value receives its own cap before the entire
        // request summary is capped again by maximumDetailBytes.
        std::size_t maximumArgumentValueBytes{ 256 };
    };


    // AgentJournal is a worker-thread-owned bounded black-box journal.
    //
    // OWNERSHIP / LIFETIME
    // --------------------
    // main() owns one AgentJournal for the conversation worker. AgentLoop borrows
    // it. The journal owns only small value events and never owns tools, model
    // providers, artifacts, or OS resources.
    //
    // PERSISTENCE
    // -----------
    // Nothing is written to disk in this checkpoint. This is deliberate: first we
    // prove the event model and bounded-memory behavior. A later crash-journal layer
    // can serialize a snapshot explicitly.
    class AgentJournal final
    {
    public:
        explicit AgentJournal(
            AgentJournalConfig config = {});

        AgentJournal(const AgentJournal&) = delete;
        AgentJournal& operator=(const AgentJournal&) = delete;

        AgentJournal(AgentJournal&&) = delete;
        AgentJournal& operator=(AgentJournal&&) = delete;

        // Allocates a monotonically increasing run id and records RunStarted.
        [[nodiscard]]
        std::uint64_t beginRun(
            std::string_view userRequest);

        void record(
            AgentEvent event);

        // Convenience helper for events associated with an exact ToolRequest.
        // Arguments are sorted before formatting so diagnostics remain stable.
        void recordToolRequest(
            std::uint64_t runId,
            AgentEventType type,
            std::size_t stepIndex,
            const tools::ToolRequest& request,
            std::string_view message = {},
            std::chrono::milliseconds duration = {});

        [[nodiscard]]
        std::vector<AgentEvent> snapshot() const;

        // Human-readable developer surface used by /agentlog.
        [[nodiscard]]
        std::string formatRecent(
            std::size_t maximumEvents = 40) const;

        void clear() noexcept;

        [[nodiscard]]
        std::size_t size() const noexcept;

        [[nodiscard]]
        std::size_t capacity() const noexcept;

    private:
        [[nodiscard]]
        std::string boundText(
            std::string_view text,
            std::size_t maximumBytes) const;

        [[nodiscard]]
        std::string formatToolRequest(
            const tools::ToolRequest& request) const;

        AgentJournalConfig config_;

        // Ring storage. We reserve the configured capacity once and reuse it.
        std::vector<AgentEvent> events_;
        std::size_t nextWriteIndex_{ 0 };

        std::uint64_t nextSequence_{ 1 };
        std::uint64_t nextRunId_{ 1 };
    };


    [[nodiscard]]
    const char* agentEventTypeName(
        AgentEventType type) noexcept;

} // namespace rose::agent
