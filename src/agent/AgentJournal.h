#pragma once

#include "tools/ToolTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rose::agent
{
    // Structured events emitted by Rose's bounded Agent and local execution paths.
    //
    // AgentJournal began as an AgentLoop-only diagnostic surface. Batch 26 widens
    // the event vocabulary slightly so non-model local operations can use the same
    // bounded black-box trail without inventing another logging subsystem.
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
        RunFailed,
        OperationStarted,
        OperationFinished,
        OperationFailed,
        RepairPlanned,
        RepairApplied,
        RepairValidated,
        RepairValidationFailed,
        CodingPlanRequired,
        CodingPlanCreated
    };


    struct AgentRunProvenance
    {
        std::string projectId;
        std::string discussionId;
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

        // Empty for events that are not associated with a tool/capability.
        std::string toolId;

        // Provider-neutral workspace provenance. These ids are intentionally copied
        // as strings so the journal does not own WorkspaceRepository state.
        std::string projectId;
        std::string discussionId;

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


    // Optional durable store. AgentJournal treats persistence as best-effort: a
    // disk error must never make a safe tool action fail after policy approval.
    // Persistence health is surfaced through /agentlog instead.
    class IAgentJournalStore
    {
    public:
        virtual ~IAgentJournalStore() = default;

        [[nodiscard]]
        virtual std::vector<AgentEvent> load() = 0;

        virtual void save(
            const std::vector<AgentEvent>& events) = 0;
    };


    // Worker-thread-owned bounded black-box journal.
    //
    // OWNERSHIP / LIFETIME
    // --------------------
    // main() owns one AgentJournal for the conversation worker. AgentLoop and the
    // shared ToolExecutionService borrow it. The optional IAgentJournalStore is
    // also borrowed and must outlive the journal.
    //
    // PERSISTENCE
    // -----------
    // When a store is supplied, the bounded snapshot is restored on launch and
    // rewritten after each event. Failures are retained as diagnostics instead of
    // aborting tool execution.
    class AgentJournal final
    {
    public:
        explicit AgentJournal(
            AgentJournalConfig config = {},
            IAgentJournalStore* store = nullptr);

        AgentJournal(const AgentJournal&) = delete;
        AgentJournal& operator=(const AgentJournal&) = delete;

        AgentJournal(AgentJournal&&) = delete;
        AgentJournal& operator=(AgentJournal&&) = delete;

        // Allocates a monotonically increasing run id and records RunStarted.
        [[nodiscard]]
        std::uint64_t beginRun(
            std::string_view userRequest,
            AgentRunProvenance provenance = {});

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

        [[nodiscard]]
        const std::string& persistenceError() const noexcept;

    private:
        [[nodiscard]]
        std::string boundText(
            std::string_view text,
            std::size_t maximumBytes) const;

        [[nodiscard]]
        std::string formatToolRequest(
            const tools::ToolRequest& request) const;

        void restoreBestEffort() noexcept;
        void persistBestEffort() noexcept;

        AgentJournalConfig config_;
        IAgentJournalStore* store_{ nullptr };

        // Ring storage. We reserve the configured capacity once and reuse it.
        std::vector<AgentEvent> events_;
        std::size_t nextWriteIndex_{ 0 };

        std::uint64_t nextSequence_{ 1 };
        std::uint64_t nextRunId_{ 1 };

        // Active provenance exists only for currently-running/pending Agent runs.
        // Completed/failed runs are removed to keep this map bounded.
        std::unordered_map<std::uint64_t, AgentRunProvenance> runProvenance_;
        std::string persistenceError_;
    };


    [[nodiscard]]
    const char* agentEventTypeName(
        AgentEventType type) noexcept;

} // namespace rose::agent
