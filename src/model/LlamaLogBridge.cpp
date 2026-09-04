#include "model/LlamaLogBridge.h"

#include "logging/Logger.h"

#include "llama.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>


namespace rose::model
{

    // -----------------------------------------------------------------------------
    // LlamaLogBridge::Impl
    // -----------------------------------------------------------------------------
    //
    // llama.cpp may emit complete lines or fragments. Some progress messages are
    // delivered using GGML_LOG_LEVEL_CONT, meaning "continue the previous log".
    //
    // We therefore collect fragments into pending_ and forward complete lines to
    // Rose's logger.
    //
    // IMPORTANT:
    //
    // llama_log_set() installs process-level callback state. For Rose v0.1 we have
    // one llama runtime and one bridge, which makes this ownership model safe.
    //
    // If Rose later supports multiple simultaneous llama runtimes, this should move
    // into a process-level LlamaRuntime service rather than each provider trying to
    // install its own callback.
    struct LlamaLogBridge::Impl
    {
        explicit Impl(
            logging::Logger& logger)
            : logger_{ logger }
        {
            // user_data points back to this Impl.
            //
            // The containing LlamaLogBridge must therefore remain alive for as long
            // as llama.cpp might emit logging callbacks.
            llama_log_set(
                &Impl::callback,
                this);
        }


        ~Impl() noexcept
        {
            // Flush any final unterminated fragment before disconnecting.
            try
            {
                std::scoped_lock lock{ mutex_ };

                flushPending();
            }
            catch (...)
            {
                // A C logging callback must never allow diagnostic failures to
                // destabilize application shutdown.
            }


            // Restore llama.cpp's default logging behavior.
            //
            // This happens after Rose's llama provider is destroyed, provided the
            // objects are declared in the order shown later in main.cpp.
            llama_log_set(
                nullptr,
                nullptr);
        }


        // -------------------------------------------------------------------------
        // callback()
        // -------------------------------------------------------------------------
        //
        // C ABI entry point invoked by llama.cpp.
        //
        // No exception may escape this function. Throwing through a C callback
        // boundary would be unsafe.
        static void callback(
            const ggml_log_level level,
            const char* text,
            void* userData) noexcept
        {
            if (userData == nullptr || text == nullptr)
            {
                return;
            }


            auto* self =
                static_cast<Impl*>(userData);


            try
            {
                self->consume(
                    level,
                    text);
            }
            catch (...)
            {
                // Logging must never be capable of crashing inference.
            }
        }


        void consume(
            const ggml_log_level level,
            const std::string_view text)
        {
            std::scoped_lock lock{ mutex_ };


            // A non-CONT event begins a new logical message.
            //
            // If llama.cpp previously left an unterminated fragment behind, flush
            // it before beginning the next message.
            if (
                level != GGML_LOG_LEVEL_CONT
                && !pending_.empty())
            {
                flushPending();
            }


            if (level != GGML_LOG_LEVEL_CONT)
            {
                pendingLevel_ = level;
            }


            pending_.append(
                text.data(),
                text.size());


            // Keep the bridge itself bounded too.
            //
            // Rose's Logger already bounds retained messages, but without this
            // guard a third-party library could theoretically feed us an enormous
            // unterminated line.
            constexpr std::size_t maxPendingBytes{ 8192 };

            if (pending_.size() > maxPendingBytes)
            {
                flushPending();
                return;
            }


            flushCompleteLines();
        }


        void flushCompleteLines()
        {
            for (;;)
            {
                const std::size_t newline =
                    pending_.find('\n');


                if (newline == std::string::npos)
                {
                    return;
                }


                std::string line =
                    pending_.substr(
                        0,
                        newline);


                // Remove Windows-style CR from CRLF if present.
                if (
                    !line.empty()
                    && line.back() == '\r')
                {
                    line.pop_back();
                }


                pending_.erase(
                    0,
                    newline + 1);


                if (!line.empty())
                {
                    forward(
                        pendingLevel_,
                        line);
                }
            }
        }


        void flushPending()
        {
            if (pending_.empty())
            {
                return;
            }


            std::string line =
                std::move(pending_);

            pending_.clear();


            if (!line.empty())
            {
                forward(
                    pendingLevel_,
                    line);
            }
        }


        void forward(
            const ggml_log_level level,
            const std::string_view message)
        {
            // ---------------------------------------------------------------------
            // Third-party log policy
            // ---------------------------------------------------------------------
            //
            // Most llama.cpp startup information is developer-level implementation
            // detail rather than something a Rose user needs to see.
            //
            // Therefore:
            //
            //     llama ERROR -> Rose Error
            //     everything else -> Rose Debug
            //
            // Normal mode becomes quiet.
            // Verbose mode retains the complete llama diagnostic stream.
            //
            // Later we can promote specific meaningful conditions to Rose warnings
            // ourselves, using provider-neutral descriptions.
            if (level == GGML_LOG_LEVEL_ERROR)
            {
                logger_.error(
                    "llama.cpp",
                    message);

                return;
            }


            logger_.debug(
                "llama.cpp",
                message);
        }


        logging::Logger& logger_;

        std::mutex mutex_;

        std::string pending_;

        ggml_log_level pendingLevel_{
            GGML_LOG_LEVEL_DEBUG
        };
    };


    // -----------------------------------------------------------------------------
    // LlamaLogBridge
    // -----------------------------------------------------------------------------

    LlamaLogBridge::LlamaLogBridge(
        logging::Logger& logger)
        : impl_{
            std::make_unique<Impl>(
                logger)
        }
    {
    }


    LlamaLogBridge::~LlamaLogBridge() = default;

} // namespace rose::model