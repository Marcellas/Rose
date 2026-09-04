#include "logging/Logger.h"

#include <iostream>
#include <stdexcept>
#include <utility>


namespace rose::logging
{

    Logger::Logger(LoggerConfig config)
        : config_{ config }
        , mode_{ config.mode }
        , events_(config.capacity)
    {
        if (config_.capacity == 0)
        {
            throw std::invalid_argument{
                "Logger capacity must be greater than zero."
            };
        }

        if (config_.maxMessageBytes == 0)
        {
            throw std::invalid_argument{
                "Logger maxMessageBytes must be greater than zero."
            };
        }

        if (config_.maxSourceBytes == 0)
        {
            throw std::invalid_argument{
                "Logger maxSourceBytes must be greater than zero."
            };
        }
    }


    void Logger::setMode(const LogMode mode) noexcept
    {
        mode_.store(
            mode,
            std::memory_order_relaxed);
    }


    LogMode Logger::mode() const noexcept
    {
        return mode_.load(
            std::memory_order_relaxed);
    }


    void Logger::debug(
        const std::string_view source,
        const std::string_view message)
    {
        write(
            LogLevel::Debug,
            source,
            message);
    }


    void Logger::info(
        const std::string_view source,
        const std::string_view message)
    {
        write(
            LogLevel::Info,
            source,
            message);
    }


    void Logger::warning(
        const std::string_view source,
        const std::string_view message)
    {
        write(
            LogLevel::Warning,
            source,
            message);
    }


    void Logger::error(
        const std::string_view source,
        const std::string_view message)
    {
        write(
            LogLevel::Error,
            source,
            message);
    }


    void Logger::write(
        const LogLevel level,
        const std::string_view source,
        const std::string_view message)
    {
        const LogMode currentMode =
            mode_.load(
                std::memory_order_relaxed);


        const bool capture =
            shouldCapture(
                currentMode,
                level);

        const bool print =
            shouldPrint(
                currentMode,
                level);


        // The common fast path for diagnostics that are disabled in the current
        // mode. No string allocation or mutex acquisition is needed.
        if (!capture && !print)
        {
            return;
        }


        const std::string boundedSource =
            boundedCopy(
                source,
                config_.maxSourceBytes);

        const std::string boundedMessage =
            boundedCopy(
                message,
                config_.maxMessageBytes);


        // One lock protects both ring-buffer modification and console diagnostic
        // output. This prevents multiple worker threads from interleaving individual
        // Rose log records.
        std::scoped_lock lock{ mutex_ };


        if (capture)
        {
            events_[nextWriteIndex_] = LogEvent{
                .sequence = nextSequence_++,
                .level = level,
                .source = boundedSource,
                .message = boundedMessage
            };


            nextWriteIndex_ =
                (nextWriteIndex_ + 1)
                % events_.size();


            if (storedCount_ < events_.size())
            {
                ++storedCount_;
            }
        }


        if (print)
        {
            std::cerr
                << "["
                << levelName(level)
                << "]["
                << boundedSource
                << "] "
                << boundedMessage
                << '\n';
        }
    }


    std::vector<LogEvent> Logger::snapshot() const
    {
        std::scoped_lock lock{ mutex_ };


        std::vector<LogEvent> result;

        result.reserve(storedCount_);


        if (storedCount_ == 0)
        {
            return result;
        }


        // If the buffer has not wrapped yet, valid records start at index zero.
        //
        // Once full, nextWriteIndex_ points at the oldest record because that is
        // the slot that will be overwritten next.
        const std::size_t startIndex =
            storedCount_ < events_.size()
            ? 0
            : nextWriteIndex_;


        for (
            std::size_t offset = 0;
            offset < storedCount_;
            ++offset)
        {
            const std::size_t index =
                (startIndex + offset)
                % events_.size();

            result.push_back(
                events_[index]);
        }


        return result;
    }


    std::size_t Logger::storedEventCount() const
    {
        std::scoped_lock lock{ mutex_ };

        return storedCount_;
    }


    bool Logger::shouldCapture(
        const LogMode mode,
        const LogLevel level) noexcept
    {
        switch (mode)
        {
        case LogMode::Silent:

            // Even when diagnostics are visually silent, preserve the most useful
            // black-box records for failure investigation.
            return
                level == LogLevel::Warning
                || level == LogLevel::Error;


        case LogMode::Normal:

            return
                level == LogLevel::Info
                || level == LogLevel::Warning
                || level == LogLevel::Error;


        case LogMode::Verbose:

            return true;
        }


        return false;
    }


    bool Logger::shouldPrint(
        const LogMode mode,
        const LogLevel level) noexcept
    {
        switch (mode)
        {
        case LogMode::Silent:

            return false;


        case LogMode::Normal:

            // Debug events are deliberately hidden during normal operation.
            return level != LogLevel::Debug;


        case LogMode::Verbose:

            return true;
        }


        return false;
    }


    std::string Logger::boundedCopy(
        const std::string_view text,
        const std::size_t maxBytes)
    {
        if (text.size() <= maxBytes)
        {
            return std::string{ text };
        }


        // For extremely small configured limits there is no room for an ellipsis.
        if (maxBytes <= 3)
        {
            return std::string{
                text.substr(
                    0,
                    maxBytes)
            };
        }


        // Reserve three bytes for the ASCII truncation marker.
        std::size_t end =
            maxBytes - 3;


        // UTF-8 continuation bytes always begin with binary 10xxxxxx.
        //
        // If our proposed cut lands inside a multibyte character, walk backward
        // until the prefix ends at a valid character boundary.
        while (end > 0)
        {
            const unsigned char nextByte =
                static_cast<unsigned char>(
                    text[end]);

            if ((nextByte & 0xC0U) != 0x80U)
            {
                break;
            }

            --end;
        }


        std::string result{
            text.substr(
                0,
                end)
        };

        result += "...";

        return result;
    }


    const char* Logger::levelName(
        const LogLevel level) noexcept
    {
        switch (level)
        {
        case LogLevel::Debug:
            return "Debug";

        case LogLevel::Info:
            return "Info";

        case LogLevel::Warning:
            return "Warning";

        case LogLevel::Error:
            return "Error";
        }


        return "Unknown";
    }

} // namespace rose::logging