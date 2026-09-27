#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rose::integrations
{
    struct OutlookMessageSummary
    {
        std::string id;
        std::string subject;
        std::string senderName;
        std::string senderAddress;
        std::string receivedDateTime;
        bool isRead{false};
        bool hasAttachments{false};
    };

    struct OutlookStatus
    {
        bool transportAvailable{false};
        bool configured{false};
        bool refreshCredentialPresent{false};
        std::string tenant;
    };

    enum class OutlookEventType
    {
        Status,
        DeviceCode,
        Connected,
        Disconnected,
        Messages,
        Error
    };

    struct OutlookEvent
    {
        OutlookEventType type{OutlookEventType::Status};
        std::string text;
        std::string verificationUri;
        std::string userCode;
        std::vector<OutlookMessageSummary> messages;
    };
}
