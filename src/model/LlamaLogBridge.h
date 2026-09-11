#pragma once

#include <memory>


namespace rose::logging
{
    class Logger;
}


namespace rose::model
{

    // -----------------------------------------------------------------------------
    // LlamaLogBridge
    // -----------------------------------------------------------------------------
    //
    // Connects llama.cpp's process-level logging callback to Rose's Logger.
    //
    // WHY THIS EXISTS
    // ---------------
    // llama.cpp exposes a C-style global logging callback. Rose should not spread
    // knowledge of that API through main.cpp, RoseCore, or the general logging
    // module.
    //
    // This adapter keeps llama.cpp-specific behavior inside the model layer.
    //
    // OWNERSHIP
    // ---------
    // LlamaLogBridge does NOT own Logger.
    //
    // Logger must outlive this object.
    //
    // The private Impl hides llama.cpp headers and callback types from users of this
    // header, keeping dependency boundaries clean.
    class LlamaLogBridge final
    {
    public:
        explicit LlamaLogBridge(
            logging::Logger& logger);

        ~LlamaLogBridge();


        LlamaLogBridge(const LlamaLogBridge&) = delete;
        LlamaLogBridge& operator=(const LlamaLogBridge&) = delete;

        LlamaLogBridge(LlamaLogBridge&&) = delete;
        LlamaLogBridge& operator=(LlamaLogBridge&&) = delete;


    private:
        struct Impl;

        std::unique_ptr<Impl> impl_;

    };

} // namespace rose::model