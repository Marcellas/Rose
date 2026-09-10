#pragma once


namespace rose::platform
{

    // -----------------------------------------------------------------------------
    // TtfRuntime
    // -----------------------------------------------------------------------------
    //
    // Owns one SDL_ttf initialization reference.
    //
    // LIFETIME:
    //
    //     SdlRuntime
    //         |
    //         +-- TtfRuntime
    //                 |
    //                 +-- SdlChatWindow
    //                         |
    //                         +-- fonts
    //                         +-- text engines
    //                         +-- text objects
    //
    // TtfRuntime must outlive every SDL_ttf object created by Rose.
    //
    // Keeping this separate from SdlRuntime prevents the SDL runtime wrapper from
    // gradually becoming a graphics "god object".
    class TtfRuntime final
    {
    public:
        TtfRuntime();

        ~TtfRuntime();


        TtfRuntime(const TtfRuntime&) = delete;
        TtfRuntime& operator=(const TtfRuntime&) = delete;

        TtfRuntime(TtfRuntime&&) = delete;
        TtfRuntime& operator=(TtfRuntime&&) = delete;
    };

} // namespace rose::platform