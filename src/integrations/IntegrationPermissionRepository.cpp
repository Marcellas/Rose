#include "integrations/IntegrationPermissionRepository.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace rose::integrations
{
    namespace
    {
        std::int64_t unixMillisecondsNow()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        bool isKnownCapability(const IntegrationCapability capability) noexcept
        {
            switch (capability)
            {
            case IntegrationCapability::MailRead:
            case IntegrationCapability::MailDraft:
            case IntegrationCapability::MailSend:
            case IntegrationCapability::CalendarRead:
            case IntegrationCapability::CalendarWrite:
            case IntegrationCapability::WebAccess:
                return true;
            }
            return false;
        }
    }

    IntegrationPermissionRepository::IntegrationPermissionRepository(
        IIntegrationPermissionStore& store)
        : store_{ store }
        , snapshot_{ store_.load() }
    {
        validateLoadedSnapshot();
    }

    const IntegrationPermissionSnapshot& IntegrationPermissionRepository::snapshot() const noexcept
    {
        return snapshot_;
    }

    bool IntegrationPermissionRepository::isAllowed(
        const std::string_view integrationId,
        const IntegrationCapability capability) const noexcept
    {
        if (!isKnownCapability(capability)) return false;

        std::string normalized;
        try
        {
            normalized = normalizeIntegrationId(integrationId);
        }
        catch (...)
        {
            return false;
        }

        const IntegrationPermissionRecord* record = find(normalized);
        if (record == nullptr) return false;

        return std::ranges::find(record->allowedCapabilities, capability)
            != record->allowedCapabilities.end();
    }

    void IntegrationPermissionRepository::setAllowed(
        const std::string_view integrationId,
        const IntegrationCapability capability,
        const bool allowed)
    {
        if (!isKnownCapability(capability))
        {
            throw std::invalid_argument{ "Unknown Rose integration capability." };
        }

        const std::string normalized = normalizeIntegrationId(integrationId);
        IntegrationPermissionRecord* record = findMutable(normalized);

        if (record == nullptr)
        {
            snapshot_.integrations.push_back(
                IntegrationPermissionRecord{
                    .integrationId = normalized,
                    .allowedCapabilities = {},
                    .updatedUnixMilliseconds = unixMillisecondsNow()
                });
            record = &snapshot_.integrations.back();
        }

        const auto found = std::ranges::find(record->allowedCapabilities, capability);
        const bool currentlyAllowed = found != record->allowedCapabilities.end();
        if (currentlyAllowed == allowed)
        {
            return;
        }

        if (allowed)
        {
            record->allowedCapabilities.push_back(capability);
            std::ranges::sort(
                record->allowedCapabilities,
                [](const IntegrationCapability left, const IntegrationCapability right)
                {
                    return static_cast<std::uint8_t>(left)
                        < static_cast<std::uint8_t>(right);
                });
        }
        else
        {
            record->allowedCapabilities.erase(found);
        }

        record->updatedUnixMilliseconds = unixMillisecondsNow();
        persist();
    }

    std::string IntegrationPermissionRepository::normalizeIntegrationId(
        const std::string_view integrationId)
    {
        if (integrationId.empty() || integrationId.size() > 64)
        {
            throw std::invalid_argument{ "Integration id must contain 1-64 characters." };
        }

        std::string normalized;
        normalized.reserve(integrationId.size());
        for (const char character : integrationId)
        {
            const unsigned char value = static_cast<unsigned char>(character);
            const char lowered = static_cast<char>(std::tolower(value));
            const bool valid =
                (lowered >= 'a' && lowered <= 'z')
                || (lowered >= '0' && lowered <= '9')
                || lowered == '.'
                || lowered == '-'
                || lowered == '_';

            if (!valid)
            {
                throw std::invalid_argument{
                    "Integration id may contain only letters, numbers, '.', '-', and '_'."
                };
            }
            normalized.push_back(lowered);
        }
        return normalized;
    }

    IntegrationPermissionRecord* IntegrationPermissionRepository::findMutable(
        const std::string_view normalizedId) noexcept
    {
        const auto found = std::ranges::find_if(
            snapshot_.integrations,
            [&](const IntegrationPermissionRecord& record)
            {
                return record.integrationId == normalizedId;
            });
        return found == snapshot_.integrations.end() ? nullptr : &*found;
    }

    const IntegrationPermissionRecord* IntegrationPermissionRepository::find(
        const std::string_view normalizedId) const noexcept
    {
        const auto found = std::ranges::find_if(
            snapshot_.integrations,
            [&](const IntegrationPermissionRecord& record)
            {
                return record.integrationId == normalizedId;
            });
        return found == snapshot_.integrations.end() ? nullptr : &*found;
    }

    void IntegrationPermissionRepository::validateLoadedSnapshot()
    {
        std::unordered_set<std::string> ids;
        for (IntegrationPermissionRecord& record : snapshot_.integrations)
        {
            record.integrationId = normalizeIntegrationId(record.integrationId);
            if (!ids.insert(record.integrationId).second)
            {
                throw std::runtime_error{ "Rose integration permission file contains a duplicate integration id." };
            }

            std::unordered_set<std::uint8_t> capabilities;
            for (const IntegrationCapability capability : record.allowedCapabilities)
            {
                if (!isKnownCapability(capability))
                {
                    throw std::runtime_error{ "Rose integration permission file contains an unknown capability." };
                }
                if (!capabilities.insert(static_cast<std::uint8_t>(capability)).second)
                {
                    throw std::runtime_error{ "Rose integration permission file contains a duplicate capability." };
                }
            }

            std::ranges::sort(
                record.allowedCapabilities,
                [](const IntegrationCapability left, const IntegrationCapability right)
                {
                    return static_cast<std::uint8_t>(left)
                        < static_cast<std::uint8_t>(right);
                });
        }
    }

    void IntegrationPermissionRepository::persist()
    {
        store_.save(snapshot_);
    }
}
