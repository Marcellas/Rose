#pragma once


namespace rose::platform
{

    // -----------------------------------------------------------------------------
    // SdlRuntime
    // -----------------------------------------------------------------------------
    //
    // Owns Rose's process-level SDL initialization.
    //
    // OWNERSHIP:
    //
    //     main()
    //       |
    //       +-- SdlRuntime
    //              |
    //              +-- SdlAvatar
    //              +-- future SdlChatWindow
    //
    // SDL is initialized once and remains available until all SDL presentation
    // objects have been destroyed.
    class SdlRuntime final
    {
    public:
        SdlRuntime();

        ~SdlRuntime();


        SdlRuntime(const SdlRuntime&) = delete;
        SdlRuntime& operator=(const SdlRuntime&) = delete;

        SdlRuntime(SdlRuntime&&) = delete;
        SdlRuntime& operator=(SdlRuntime&&) = delete;
    };

} // namespace rose::platform