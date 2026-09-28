#include "development/CMakeBuildService.h"
#include "development/CMakeConfigureService.h"
#include "development/CMakeTestService.h"
#include "development/DiagnosticExtraction.h"
#include "tools/BuildCMakeProjectTool.h"
#include "tools/ReconfigureCMakeProjectTool.h"
#include "tools/RunCMakeTestsTool.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(const bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error{ message };
    }

    class FakeCMakeBuildService final : public rose::development::ICMakeBuildService
    {
    public:
        rose::development::CMakeBuildRequest lastRequest;
        rose::development::CMakeBuildResult nextResult;

        rose::development::CMakeBuildResult build(
            const rose::development::CMakeBuildRequest& request) override
        {
            lastRequest = request;
            return nextResult;
        }
    };

    class FakeCMakeConfigureService final : public rose::development::ICMakeConfigureService
    {
    public:
        rose::development::CMakeConfigureRequest lastRequest;
        rose::development::CMakeConfigureResult nextResult;

        rose::development::CMakeConfigureResult reconfigure(
            const rose::development::CMakeConfigureRequest& request) override
        {
            lastRequest = request;
            return nextResult;
        }
    };


    class FakeCMakeTestService final : public rose::development::ICMakeTestService
    {
    public:
        rose::development::CMakeTestRequest lastRequest;
        rose::development::CMakeTestResult nextResult;

        rose::development::CMakeTestResult run(
            const rose::development::CMakeTestRequest& request) override
        {
            lastRequest = request;
            return nextResult;
        }
    };
}

int main()
{
    try
    {
        const std::filesystem::path source =
            std::filesystem::temp_directory_path() / "rose-development-tools-test";


        // ------------------------------------------------------------------
        // Structured source diagnostic extraction
        // ------------------------------------------------------------------
        const std::filesystem::path diagnosticRoot =
            std::filesystem::temp_directory_path() / "rose-diagnostic-extraction-test";
        const std::filesystem::path outsideDiagnostic =
            std::filesystem::temp_directory_path() / "rose-diagnostic-outside.cpp";

        std::error_code diagnosticError;
        std::filesystem::remove_all(diagnosticRoot, diagnosticError);
        diagnosticError.clear();
        std::filesystem::create_directories(diagnosticRoot / "src", diagnosticError);
        require(!diagnosticError,
            "diagnostic extraction fixture directory must be creatable");

        const std::filesystem::path diagnosticSource = diagnosticRoot / "src" / "main.cpp";
        std::ofstream{ diagnosticSource } << "int main() { return 0; }\n";
        std::ofstream{ diagnosticRoot / "CMakeLists.txt" }
            << "cmake_minimum_required(VERSION 3.20)\n";
        std::ofstream{ outsideDiagnostic } << "secret\n";

        const std::string diagnosticOutput =
            diagnosticSource.string()
            + "(42,7): error C2065: missing_identifier\n"
            + diagnosticSource.string()
            + ":55:9: warning: example warning\n"
            + "CMake Error at CMakeLists.txt:12 (message): configuration failed\n"
            + "7: " + diagnosticSource.string()
            + ":66: Failure\n"
            + outsideDiagnostic.string()
            + "(99,1): error C9999: outside project must be ignored\n";

        const auto extracted = rose::development::extractSourceDiagnostics(
            diagnosticOutput,
            diagnosticRoot);
        require(extracted.size() == 4,
            "diagnostic extraction must keep project-local MSVC/GCC/CMake/CTest diagnostics and discard outside paths");
        require(extracted[0].path == std::filesystem::weakly_canonical(diagnosticSource)
                && extracted[0].line == 42
                && extracted[0].column == 7
                && extracted[0].severity == "error"
                && extracted[0].code == "C2065",
            "MSVC diagnostic metadata must preserve grounded path/line/column/severity/code");
        require(extracted[1].path == std::filesystem::weakly_canonical(diagnosticRoot / "CMakeLists.txt")
                && extracted[1].line == 12
                && extracted[1].severity == "error",
            "CMake source diagnostics must resolve relative project files safely");
        require(extracted[2].line == 66
                && extracted[2].severity == "test_failure",
            "CTest numeric output prefixes and test-failure locations must be normalized");
        require(extracted[3].line == 55
                && extracted[3].severity == "warning",
            "warnings should remain available after actionable errors");

        const std::string trustedDiagnostic =
            rose::development::buildTrustedDiagnosticMetadata(
                "build_cmake_project",
                false,
                extracted);
        require(trustedDiagnostic.find("metadata_kind=source_diagnostic") != std::string::npos
                && trustedDiagnostic.find("diagnostic_line=42") != std::string::npos
                && trustedDiagnostic.find("suggested_read_start_line=12") != std::string::npos
                && trustedDiagnostic.find("suggested_read_line_count=80") != std::string::npos,
            "failed validation must expose one bounded Rose-owned diagnostic source window");
        require(rose::development::buildTrustedDiagnosticMetadata(
                    "build_cmake_project",
                    true,
                    extracted).empty(),
            "successful builds must not request diagnostic source follow-up merely because warnings exist");

        std::filesystem::remove_all(diagnosticRoot, diagnosticError);
        diagnosticError.clear();
        std::filesystem::remove(outsideDiagnostic, diagnosticError);

        // ------------------------------------------------------------------
        // Controlled CMake build tool
        // ------------------------------------------------------------------
        FakeCMakeBuildService buildService;
        buildService.nextResult = rose::development::CMakeBuildResult{
            .exitCode = 1,
            .timedOut = false,
            .outputTruncated = false,
            .sourceDirectory = source,
            .buildDirectory = source / "build",
            .cmakeExecutable = "cmake.exe",
            .output = "main.cpp(42): error C2065: example",
            .diagnostics = {
                rose::development::SourceDiagnostic{
                    .path = source / "main.cpp",
                    .line = 42,
                    .column = 0,
                    .severity = "error",
                    .code = "C2065",
                    .message = "example"
                }
            }
        };

        rose::tools::BuildCMakeProjectTool buildTool{ buildService };
        require(buildTool.descriptor().risk == rose::tools::ToolRisk::ExternalEffect,
            "build_cmake_project must be ExternalEffect because build rules may execute code");
        require(buildTool.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation,
            "build_cmake_project must require confirmation");

        const rose::tools::ToolResult failedBuild = buildTool.execute(
            rose::tools::ToolRequest{
                .toolId = "build_cmake_project",
                .arguments = {
                    { "source_path", source.string() },
                    { "configuration", "Debug" },
                    { "target", "Rose" },
                    { "jobs", "8" }
                }
            });

        require(!failedBuild.success,
            "a non-zero CMake exit code must be surfaced as an unsuccessful tool result");
        require(buildService.lastRequest.sourceDirectory == source,
            "build_cmake_project must forward the exact source directory");
        require(buildService.lastRequest.configuration == "Debug",
            "build_cmake_project must preserve the requested configuration");
        require(buildService.lastRequest.target == "Rose",
            "build_cmake_project must preserve the exact target");
        require(buildService.lastRequest.parallelJobs == 8,
            "build_cmake_project must preserve the bounded parallel job count");
        require(failedBuild.message.find("error C2065") != std::string::npos,
            "compiler diagnostics must remain available to the next agent step");
        require(failedBuild.responseMode == rose::tools::ToolResponseMode::RequiresModelSynthesis,
            "build results must return to the model so Rose can reason about failures");
        require(failedBuild.trustedMetadata.find("producer_tool=build_cmake_project") != std::string::npos
                && failedBuild.trustedMetadata.find("diagnostic_line=42") != std::string::npos,
            "failed build tools must publish only structured grounded diagnostic metadata for agent routing");

        buildService.nextResult.exitCode = 0;
        buildService.nextResult.output = "RoseTextMutationTools tests: PASS";
        const rose::tools::ToolResult succeededBuild = buildTool.execute(
            rose::tools::ToolRequest{
                .toolId = "build_cmake_project",
                .arguments = {
                    { "source_path", source.string() }
                }
            });

        require(succeededBuild.success,
            "zero exit code should produce a successful build observation");
        require(buildService.lastRequest.configuration == "Debug",
            "Debug must be the deterministic default build configuration");
        require(buildService.lastRequest.parallelJobs == 8,
            "eight jobs must be the deterministic default build parallelism");

        bool badBuildJobsRejected = false;
        try
        {
            (void)buildTool.execute(
                rose::tools::ToolRequest{
                    .toolId = "build_cmake_project",
                    .arguments = {
                        { "source_path", source.string() },
                        { "jobs", "1000" }
                    }
                });
        }
        catch (const std::invalid_argument&)
        {
            badBuildJobsRejected = true;
        }
        require(badBuildJobsRejected,
            "unbounded build parallelism must fail closed");

        rose::development::LocalCMakeBuildService localBuildService;
        bool installTargetRejected = false;
        try
        {
            (void)localBuildService.build(
                rose::development::CMakeBuildRequest{
                    .sourceDirectory = source,
                    .configuration = "Debug",
                    .target = "install",
                    .parallelJobs = 8
                });
        }
        catch (const std::invalid_argument&)
        {
            installTargetRejected = true;
        }
        require(installTargetRejected,
            "installation-like CMake targets must fail before any process execution");

        // ------------------------------------------------------------------
        // Controlled existing-tree CMake reconfiguration tool
        // ------------------------------------------------------------------
        FakeCMakeConfigureService configureService;
        configureService.nextResult = rose::development::CMakeConfigureResult{
            .exitCode = 1,
            .timedOut = false,
            .outputTruncated = false,
            .sourceDirectory = source,
            .buildDirectory = source / "build",
            .cmakeExecutable = "cmake.exe",
            .output = "CMake Error at CMakeLists.txt:17 (message): bad configure",
            .diagnostics = {
                rose::development::SourceDiagnostic{
                    .path = source / "CMakeLists.txt",
                    .line = 17,
                    .column = 0,
                    .severity = "error",
                    .code = {},
                    .message = "bad configure"
                }
            }
        };

        rose::tools::ReconfigureCMakeProjectTool configureTool{ configureService };
        require(configureTool.descriptor().risk == rose::tools::ToolRisk::ExternalEffect,
            "reconfigure_cmake_project must be ExternalEffect because CMake scripts/dependency discovery may execute project-controlled effects");
        require(configureTool.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation,
            "reconfigure_cmake_project must require confirmation");
        require(configureTool.descriptor().parameters.size() == 1
                && configureTool.descriptor().parameters[0].name == "source_path",
            "reconfigure_cmake_project must expose only the exact source directory and no arbitrary CMake argument surface");

        const rose::tools::ToolResult failedConfigure = configureTool.execute(
            rose::tools::ToolRequest{
                .toolId = "reconfigure_cmake_project",
                .arguments = {
                    { "source_path", source.string() }
                }
            });

        require(!failedConfigure.success,
            "a non-zero CMake configure exit code must be surfaced as an unsuccessful tool result");
        require(configureService.lastRequest.sourceDirectory == source,
            "reconfigure_cmake_project must preserve the exact source directory");
        require(failedConfigure.responseMode == rose::tools::ToolResponseMode::RequiresModelSynthesis,
            "configure output must return to the model for diagnosis");
        require(failedConfigure.trustedMetadata.find("producer_tool=reconfigure_cmake_project") != std::string::npos
                && failedConfigure.trustedMetadata.find("diagnostic_line=17") != std::string::npos,
            "failed reconfiguration must publish grounded CMake source diagnostics for repair routing");

        configureService.nextResult.exitCode = 0;
        configureService.nextResult.output = "-- Configuring done\n-- Generating done";
        configureService.nextResult.diagnostics.clear();
        const rose::tools::ToolResult succeededConfigure = configureTool.execute(
            rose::tools::ToolRequest{
                .toolId = "reconfigure_cmake_project",
                .arguments = {
                    { "source_path", source.string() }
                }
            });
        require(succeededConfigure.success,
            "zero exit code should produce a successful CMake reconfiguration observation");

        bool configureExtraArgumentRejected = false;
        try
        {
            (void)configureTool.execute(
                rose::tools::ToolRequest{
                    .toolId = "reconfigure_cmake_project",
                    .arguments = {
                        { "source_path", source.string() },
                        { "generator", "Ninja" }
                    }
                });
        }
        catch (const std::invalid_argument&)
        {
            configureExtraArgumentRejected = true;
        }
        require(configureExtraArgumentRejected,
            "reconfigure_cmake_project must reject generator/cache/toolchain-like extra authority");

        const std::filesystem::path unconfiguredSource =
            std::filesystem::temp_directory_path() / "rose-unconfigured-cmake-test";
        std::error_code configureFixtureError;
        std::filesystem::remove_all(unconfiguredSource, configureFixtureError);
        configureFixtureError.clear();
        std::filesystem::create_directories(unconfiguredSource / "build", configureFixtureError);
        require(!configureFixtureError,
            "unconfigured CMake fixture must be creatable");
        std::ofstream{ unconfiguredSource / "CMakeLists.txt" }
            << "cmake_minimum_required(VERSION 3.20)\nproject(RoseConfigureFixture)\n";

        rose::development::LocalCMakeConfigureService localConfigureService;
        bool missingCacheRejected = false;
        try
        {
            (void)localConfigureService.reconfigure(
                rose::development::CMakeConfigureRequest{
                    .sourceDirectory = unconfiguredSource
                });
        }
        catch (const std::runtime_error&)
        {
            missingCacheRejected = true;
        }
        require(missingCacheRejected,
            "CMake reconfiguration must fail before process execution when the existing build cache is absent");
        std::filesystem::remove_all(unconfiguredSource, configureFixtureError);


        // ------------------------------------------------------------------
        // Controlled CTest tool
        // ------------------------------------------------------------------
        FakeCMakeTestService testService;
        testService.nextResult = rose::development::CMakeTestResult{
            .exitCode = 8,
            .timedOut = false,
            .outputTruncated = false,
            .sourceDirectory = source,
            .buildDirectory = source / "build",
            .ctestExecutable = "ctest.exe",
            .output = "1/1 Test #1: RoseTextMutationToolsTest ...***Failed",
            .diagnostics = {
                rose::development::SourceDiagnostic{
                    .path = source / "TextMutationTools_test.cpp",
                    .line = 77,
                    .column = 3,
                    .severity = "test_failure",
                    .code = {},
                    .message = "assertion failed"
                }
            }
        };

        rose::tools::RunCMakeTestsTool testTool{ testService };
        require(testTool.descriptor().risk == rose::tools::ToolRisk::ExternalEffect,
            "run_cmake_tests must be ExternalEffect because tests execute project code");
        require(testTool.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation,
            "run_cmake_tests must require confirmation");

        const rose::tools::ToolResult failedTest = testTool.execute(
            rose::tools::ToolRequest{
                .toolId = "run_cmake_tests",
                .arguments = {
                    { "source_path", source.string() },
                    { "configuration", "Debug" },
                    { "test", "RoseTextMutationToolsTest" },
                    { "jobs", "4" }
                }
            });

        require(!failedTest.success,
            "a non-zero CTest exit code must be surfaced as an unsuccessful tool result");
        require(testService.lastRequest.sourceDirectory == source,
            "run_cmake_tests must forward the exact source directory");
        require(testService.lastRequest.configuration == "Debug",
            "run_cmake_tests must preserve the requested configuration");
        require(testService.lastRequest.testName == "RoseTextMutationToolsTest",
            "run_cmake_tests must preserve the exact registered test name");
        require(testService.lastRequest.parallelJobs == 4,
            "run_cmake_tests must preserve bounded requested parallelism");
        require(failedTest.message.find("***Failed") != std::string::npos,
            "CTest diagnostics must remain available to the next agent step");
        require(failedTest.responseMode == rose::tools::ToolResponseMode::RequiresModelSynthesis,
            "test results must return to the model for diagnosis");
        require(failedTest.trustedMetadata.find("producer_tool=run_cmake_tests") != std::string::npos
                && failedTest.trustedMetadata.find("diagnostic_line=77") != std::string::npos,
            "failed CTest tools must publish a bounded grounded diagnostic source window when available");

        testService.nextResult.exitCode = 0;
        testService.nextResult.output = "100% tests passed";
        const rose::tools::ToolResult succeededTest = testTool.execute(
            rose::tools::ToolRequest{
                .toolId = "run_cmake_tests",
                .arguments = {
                    { "source_path", source.string() }
                }
            });

        require(succeededTest.success,
            "zero CTest exit code should produce a successful test observation");
        require(testService.lastRequest.configuration == "Debug",
            "Debug must be the deterministic default test configuration");
        require(testService.lastRequest.testName.empty(),
            "omitted test name must mean all registered tests");
        require(testService.lastRequest.parallelJobs == 8,
            "eight jobs must be the deterministic default test parallelism");

        bool badTestJobsRejected = false;
        try
        {
            (void)testTool.execute(
                rose::tools::ToolRequest{
                    .toolId = "run_cmake_tests",
                    .arguments = {
                        { "source_path", source.string() },
                        { "jobs", "0" }
                    }
                });
        }
        catch (const std::invalid_argument&)
        {
            badTestJobsRejected = true;
        }
        require(badTestJobsRejected,
            "unbounded/zero CTest parallelism must fail closed");

        rose::development::LocalCMakeTestService localTestService;
        bool unsafeTestNameRejected = false;
        try
        {
            (void)localTestService.run(
                rose::development::CMakeTestRequest{
                    .sourceDirectory = source,
                    .configuration = "Debug",
                    .testName = "Rose.*;launch",
                    .parallelJobs = 8
                });
        }
        catch (const std::invalid_argument&)
        {
            unsafeTestNameRejected = true;
        }
        require(unsafeTestNameRejected,
            "CTest names with regex/shell-like punctuation outside the allowlist must fail before filesystem/process access");

        std::cout << "Rose DevelopmentTools tests: PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rose DevelopmentTools tests: FAIL: " << exception.what() << '\n';
        return 1;
    }
}
