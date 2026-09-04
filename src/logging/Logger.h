#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>


namespace rose::logging
{

    // -----------------------------------------------------------------------------
    // LogMode
    // -----------------------------------------------------------------------------
    //
    // Controls how much diagnostic information Rose retains and displays.
    //
    // Silent:
    //     No diagnostic console output.
    //     Warnings and errors are still retained in the bounded black-box history.
    //
    // Normal:
    //     Info, warnings, and errors are retained and displayed.
    //     Debug/trace noise is suppressed.
    //
    // Verbose:
    //     Everything is retained and displayed.
    //
    // This distinction lets Rose remain quiet during normal use while still
    // preserving a small amount of useful failure information.
    enum class LogMode : std::uint8_t
    {
        Silent,
        Normal,
        Verbose
    };


    // -----------------------------------------------------------------------------
    // LogLevel
    // -----------------------------------------------------------------------------
    //
    // Severity of one diagnostic event.
    //
    // Debug is intentionally the lowest severity. Third-party diagnostic chatter,
    // such as most llama.cpp initialization details, can later be mapped here so it
    // only appears when Rose is in Verbose mode.
    enum class LogLevel : std::uint8_t
    {
        Debug,
        Info,
        Warning,
        Error
    };


    // -----------------------------------------------------------------------------
    // LogEvent
    // -----------------------------------------------------------------------------
    //
    // One structured diagnostic record.
    //
    // We store the source separately from the message so future crash reports,
    // filtering, UI diagnostics, and persistent logs do not have to parse formatted
    // console strings.
    //
    // Example:
    //
    //     source  = "LlamaCpp"
    //     level   = LogLevel::Warning
    //     message = "Context window smaller than training context."
    struct LogEvent
    {
        std::uint64_t sequence{ 0 };

        LogLevel level{ LogLevel::Info };

        std::string source;

        std::string message;
    };


    // -----------------------------------------------------------------------------
    // LoggerConfig
    // -----------------------------------------------------------------------------
    //
    // capacity:
    //     Maximum number of events retained in memory.
    //
    // maxMessageBytes:
    //     Hard upper bound for one stored message.
    //
    // maxSourceBytes:
    //     Hard upper bound for the diagnostic source name.
    //
    // Both the event count and individual string size are bounded so the logger
    // cannot silently consume unlimited memory during a long-running Rose session.
    struct LoggerConfig
    {
        LogMode mode{ LogMode::Normal };

        std::size_t capacity{ 512 };

        std::size_t maxMessageBytes{ 4096 };

        std::size_t maxSourceBytes{ 64 };
    };


    // -----------------------------------------------------------------------------
    // Logger
    // -----------------------------------------------------------------------------
    //
    // Rose's bounded in-memory black-box diagnostic logger.
    //
    // OWNERSHIP
    // ---------
    // Logger owns its ring buffer and all strings stored inside it.
    //
    // Other systems should borrow Logger by reference. RoseCore, model providers,
    // tools, memory, and the UI should not own separate global loggers.
    //
    // THREADING
    // ---------
    // Logging is protected by a mutex because inference, tools, indexing, and the UI
    // will eventually operate on different threads.
    //
    // Changing LogMode uses an atomic because that value is read frequently and is
    // tiny.
    //
    // MEMORY
    // ------
    // The ring buffer is allocated once at construction. When full, new events
    // overwrite the oldest events rather than growing the container.
    //
    // This is the same general "black box" behavior we want for Rose: recent
    // diagnostic history survives while ancient noise naturally falls away.
    class Logger final
    {
    public:
        explicit Logger(LoggerConfig config = {});


        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;


        void setMode(LogMode mode) noexcept;

        [[nodiscard]]
        LogMode mode() const noexcept;


        void debug(
            std::string_view source,
            std::string_view message);

        void info(
            std::string_view source,
            std::string_view message);

        void warning(
            std::string_view source,
            std::string_view message);

        void error(
            std::string_view source,
            std::string_view message);


        // Return an owning chronological copy of the currently retained events.
        //
        // This intentionally copies the data. A future crash-dump writer can safely
        // serialize the snapshot without keeping Logger's mutex locked.
        [[nodiscard]]
        std::vector<LogEvent> snapshot() const;


        [[nodiscard]]
        std::size_t storedEventCount() const;


    private:
        void write(
            LogLevel level,
            std::string_view source,
            std::string_view message);


        [[nodiscard]]
        static bool shouldCapture(
            LogMode mode,
            LogLevel level) noexcept;


        [[nodiscard]]
        static bool shouldPrint(
            LogMode mode,
            LogLevel level) noexcept;


        // Copy a UTF-8 string while keeping its stored byte count bounded.
        //
        // The implementation avoids cutting through the middle of a UTF-8 code
        // point when truncation is required.
        [[nodiscard]]
        static std::string boundedCopy(
            std::string_view text,
            std::size_t maxBytes);


        [[nodiscard]]
        static const char* levelName(
            LogLevel level) noexcept;


        LoggerConfig config_;

        std::atomic<LogMode> mode_;

        mutable std::mutex mutex_;


        // Fixed-size ring storage.
        std::vector<LogEvent> events_;

        // Slot that receives the next captured event.
        std::size_t nextWriteIndex_{ 0 };

        // Number of valid events currently stored.
        std::size_t storedCount_{ 0 };

        // Monotonically increasing identifier useful for reconstructing order.
        std::uint64_t nextSequence_{ 1 };
    };

} // namespace rose::logging