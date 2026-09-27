#include "integrations/FileIntegrationPermissionStore.h"
#include "integrations/IntegrationCommand.h"
#include "integrations/IntegrationPermissionRepository.h"
#include "integrations/WindowsCredentialVault.h"
#include "permissions/ToolExecutionPolicy.h"
#include "tools/ToolTypes.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error{ message };
    }
}

int main()
{
    try
    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / "rose-integration-system-test";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(root);
        const std::filesystem::path path = root / "permissions.roseperm";

        {
            rose::integrations::FileIntegrationPermissionStore store{ path };
            rose::integrations::IntegrationPermissionRepository repository{ store };

            require(!repository.isAllowed(
                "outlook", rose::integrations::IntegrationCapability::MailRead),
                "integration capabilities must fail closed by default");

            repository.setAllowed(
                "Outlook",
                rose::integrations::IntegrationCapability::MailRead,
                true);
            repository.setAllowed(
                "outlook",
                rose::integrations::IntegrationCapability::CalendarRead,
                true);

            require(repository.isAllowed(
                "OUTLOOK", rose::integrations::IntegrationCapability::MailRead),
                "integration ids should be normalized case-insensitively");
            require(repository.snapshot().integrations.size() == 1,
                "normalized integration ids should share one record");
        }

        {
            rose::integrations::FileIntegrationPermissionStore store{ path };
            rose::integrations::IntegrationPermissionRepository repository{ store };
            require(repository.isAllowed(
                "outlook", rose::integrations::IntegrationCapability::MailRead),
                "mail.read grant should survive reload");
            require(repository.isAllowed(
                "outlook", rose::integrations::IntegrationCapability::CalendarRead),
                "calendar.read grant should survive reload");

            repository.setAllowed(
                "outlook",
                rose::integrations::IntegrationCapability::MailRead,
                false);
            require(!repository.isAllowed(
                "outlook", rose::integrations::IntegrationCapability::MailRead),
                "explicit deny should remove a local grant");
        }

        const auto allow = rose::integrations::parseIntegrationCommand(
            "/integration allow outlook mail.read");
        require(allow.has_value(), "allow command should parse");
        require(allow->kind == rose::integrations::IntegrationCommandKind::Allow,
            "allow command kind mismatch");
        require(allow->capability == rose::integrations::IntegrationCapability::MailRead,
            "allow command capability mismatch");

        const auto list = rose::integrations::parseIntegrationCommand("/integrations");
        require(list.has_value()
            && list->kind == rose::integrations::IntegrationCommandKind::List,
            "list command should parse");

        rose::integrations::WindowsCredentialVault vault;
#if defined(_WIN32)
        require(vault.available(), "Windows credential vault should be available on Windows");
#else
        require(!vault.available(), "Windows credential vault should report unavailable off Windows");
#endif

        // A local capability grant is never permission to silently perform an
        // externally consequential operation. The existing central tool policy
        // remains the second, exact-action gate.
        rose::permissions::ToolExecutionPolicy executionPolicy;
        rose::tools::ToolDescriptor externalTool{
            .id = "integration_test_send",
            .displayName = "Integration test send",
            .description = "Test-only external effect",
            .risk = rose::tools::ToolRisk::ExternalEffect,
            .consent = rose::tools::ToolConsent::AutoAllowed,
            .parameters = {}
        };

        require(
            executionPolicy.evaluate(externalTool).disposition
                == rose::permissions::ToolExecutionDisposition::RequiresConfirmation,
            "external effects must require exact user confirmation even when misconfigured auto-allowed");

        require(
            executionPolicy.evaluate(
                externalTool,
                rose::permissions::ToolConfirmationState::ExplicitlyConfirmed).allowed(),
            "explicit confirmation should allow the exact pending external action");

        std::filesystem::remove_all(root, error);
        std::cout << "Rose IntegrationSystem tests: PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rose IntegrationSystem tests: FAIL: " << exception.what() << '\n';
        return 1;
    }
}
