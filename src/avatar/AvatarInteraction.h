#pragma once

#include <cstdint>

namespace rose::avatar
{
    enum class AvatarInteractionKind : std::uint8_t
    {
        PrimaryClick,
        ContextMenuRequested,
        DragStarted,
        DragEnded
    };

    struct AvatarInteractionEvent
    {
        AvatarInteractionKind kind{ AvatarInteractionKind::PrimaryClick };
        float globalX{ 0.0f };
        float globalY{ 0.0f };
    };
}
