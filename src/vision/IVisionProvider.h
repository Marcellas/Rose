#pragma once

#include "vision/VisionTypes.h"

#include <string>

namespace rose::vision
{

    // Provider-neutral semantic vision boundary.
    //
    // OCR answers "what text is visible?".
    // A vision provider answers broader pixel-grounded questions such as:
    //
    //     - what objects/people/scenes are visible?
    //     - what is unusual or damaged?
    //     - what does a chart/diagram depict?
    //     - what visual detail is relevant to the user's question?
    //
    // RoseCore does not depend on this interface directly. Attachment ingestion
    // uses it to convert an explicitly attached image into request-local source
    // material for Rose's normal text model.
    class IVisionProvider
    {
    public:
        virtual ~IVisionProvider() = default;

        [[nodiscard]]
        virtual bool available() const noexcept = 0;

        [[nodiscard]]
        virtual std::string availabilityMessage() const = 0;

        [[nodiscard]]
        virtual VisionResult analyze(
            const VisionRequest& request) = 0;
    };

} // namespace rose::vision
