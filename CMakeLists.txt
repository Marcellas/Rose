cmake_minimum_required(VERSION 3.24)

project(
    Rose
    VERSION 0.1.0
    LANGUAGES CXX
)


# =============================================================================
# Rose-owned target helpers
# =============================================================================
#
# Keep Rose's build policy on Rose-owned targets only. Third-party dependencies
# retain their own language standards and warning settings.

function(rose_configure_cpp20 target)
    target_compile_features(
        ${target}
        PRIVATE
            cxx_std_20
    )

    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
    )
endfunction()


function(rose_enable_warnings target)
    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                /W4
                /permissive-
                /Zc:__cplusplus
                /utf-8
        )
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )
    endif()
endfunction()


function(rose_copy_runtime_dlls target)
    if(WIN32)
        add_custom_command(
            TARGET ${target}
            POST_BUILD

            COMMAND
                ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>

            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()


# =============================================================================
# llama.cpp
# =============================================================================
#
# Do not set CMAKE_CXX_STANDARD globally. Rose uses C++20, but vendored
# dependencies must remain free to select their own language standards.
#
# CUDA is an implementation detail of the current local llama.cpp provider.
# Rose's ModelProvider abstraction itself remains independent of CUDA.

set(GGML_CUDA ON CACHE BOOL "" FORCE)

set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)

# RoseVisionWorker links libmtmd directly without enabling llama.cpp's tools.
set(LLAMA_BUILD_MTMD ON CACHE BOOL "" FORCE)

# Still-image support only for now.
set(MTMD_VIDEO OFF CACHE BOOL "" FORCE)

add_subdirectory(
    external/llama.cpp
    EXCLUDE_FROM_ALL
)


# =============================================================================
# SDL3
# =============================================================================

add_subdirectory(
    external/SDL
    EXCLUDE_FROM_ALL
)


# =============================================================================
# SDL_ttf
# =============================================================================
#
# HarfBuzz remains enabled for shaping. PlutoSVG is disabled until Rose needs
# SVG/color-emoji support.

set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)

add_subdirectory(
    external/SDL_ttf
    EXCLUDE_FROM_ALL
)


# =============================================================================
# SDL_image
# =============================================================================
#
# Rose currently needs PNG only. Keep the dependency surface deliberately small.

set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)

set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)

set(SDLIMAGE_ANI OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BMP OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JPG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_LBM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PCX OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_QOI OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SVG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TGA OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XCF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XPM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XV OFF CACHE BOOL "" FORCE)

set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)

# SDL3 already provides the PNG path Rose needs, so avoid carrying an additional
# vendored libpng/zlib stack through SDL_image.
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_SAVE OFF CACHE BOOL "" FORCE)

add_subdirectory(
    external/SDL_image
    EXCLUDE_FROM_ALL
)


# =============================================================================
# MicroTeX
# =============================================================================
#
# MicroTeX uses add_library(LaTeX "") without explicitly choosing STATIC/SHARED.
# SDL dependencies may enable BUILD_SHARED_LIBS, which would incorrectly turn
# MicroTeX into a Windows DLL without a usable import library.
#
# Force static behavior only while configuring MicroTeX, then restore the prior
# BUILD_SHARED_LIBS state.

set(HAVE_LOG OFF CACHE BOOL "" FORCE)
set(GRAPHICS_DEBUG OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)

if(DEFINED BUILD_SHARED_LIBS)
    set(ROSE_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(ROSE_HAD_BUILD_SHARED_LIBS TRUE)
else()
    set(ROSE_HAD_BUILD_SHARED_LIBS FALSE)
endif()

set(BUILD_SHARED_LIBS OFF)

add_subdirectory(
    external/MicroTex
    EXCLUDE_FROM_ALL
)

if(ROSE_HAD_BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS "${ROSE_SAVED_BUILD_SHARED_LIBS}")
else()
    unset(BUILD_SHARED_LIBS)
endif()

unset(ROSE_SAVED_BUILD_SHARED_LIBS)
unset(ROSE_HAD_BUILD_SHARED_LIBS)


if(WIN32)
    # Prevent MicroTeX's _HAS_STD_BYTE=0 definition from propagating into Rose.
    set_property(
        TARGET LaTeX
        PROPERTY INTERFACE_COMPILE_DEFINITIONS
            BUILD_WIN32
    )

    # MicroTeX does not add its Win32 graphics implementation to LaTeX itself.
    target_sources(
        LaTeX
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/external/MicroTex/src/platform/gdi_win/graphic_win32.cpp"
    )

    target_link_libraries(
        LaTeX
        PRIVATE
            gdiplus
    )
endif()


# =============================================================================
# PDFium (optional)
# =============================================================================
#
# Rose degrades gracefully when the prebuilt PDFium package is absent.
#
# When all expected files are present, PDF attachment extraction is enabled.
# Otherwise Rose still configures/builds normally and PdfTextExtractor reports a
# request-local "PDF support is unavailable in this build" error if a PDF is
# attached.
#
# Expected optional layout:
#
#   external/pdfium/include/fpdfview.h
#   external/pdfium/include/fpdf_text.h
#   external/pdfium/include/fpdf_edit.h
#   external/pdfium/include/fpdf_save.h
#   external/pdfium/include/fpdf_ppo.h
#   external/pdfium/include/fpdf_annot.h
#   external/pdfium/lib/pdfium.dll.lib
#   external/pdfium/bin/pdfium.dll

set(ROSE_HAS_PDFIUM OFF)
set(ROSE_HAS_PDFIUM_MUTATION OFF)

if(WIN32)
    set(
        ROSE_PDFIUM_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/external/pdfium"
        CACHE PATH
        "Path to Rose's unpacked PDFium binary distribution"
    )

    set(
        ROSE_PDFIUM_INCLUDE_DIR
        "${ROSE_PDFIUM_ROOT}/include"
    )

    set(
        ROSE_PDFIUM_IMPORT_LIBRARY
        "${ROSE_PDFIUM_ROOT}/lib/pdfium.dll.lib"
    )

    set(
        ROSE_PDFIUM_RUNTIME_DLL
        "${ROSE_PDFIUM_ROOT}/bin/pdfium.dll"
    )

    # Core read support only needs the view/text API. Keep this independent from
    # the richer mutation surface so an older PDFium package cannot regress
    # Rose's already-working PDF reader.
    if(
        EXISTS "${ROSE_PDFIUM_INCLUDE_DIR}/fpdfview.h"
        AND EXISTS "${ROSE_PDFIUM_INCLUDE_DIR}/fpdf_text.h"
        AND EXISTS "${ROSE_PDFIUM_IMPORT_LIBRARY}"
        AND EXISTS "${ROSE_PDFIUM_RUNTIME_DLL}"
    )
        add_library(
            PDFium::PDFium
            SHARED
            IMPORTED
            GLOBAL
        )

        set_target_properties(
            PDFium::PDFium
            PROPERTIES
                IMPORTED_IMPLIB "${ROSE_PDFIUM_IMPORT_LIBRARY}"
                IMPORTED_LOCATION "${ROSE_PDFIUM_RUNTIME_DLL}"
                INTERFACE_INCLUDE_DIRECTORIES "${ROSE_PDFIUM_INCLUDE_DIR}"
        )

        set(ROSE_HAS_PDFIUM ON)

        if(
            EXISTS "${ROSE_PDFIUM_INCLUDE_DIR}/fpdf_edit.h"
            AND EXISTS "${ROSE_PDFIUM_INCLUDE_DIR}/fpdf_save.h"
            AND EXISTS "${ROSE_PDFIUM_INCLUDE_DIR}/fpdf_ppo.h"
            AND EXISTS "${ROSE_PDFIUM_INCLUDE_DIR}/fpdf_annot.h"
        )
            set(ROSE_HAS_PDFIUM_MUTATION ON)
        endif()

        message(
            STATUS
            "Rose PDF read support: enabled (${ROSE_PDFIUM_ROOT})"
        )

        if(ROSE_HAS_PDFIUM_MUTATION)
            message(STATUS "Rose PDF mutation support: enabled")
        else()
            message(STATUS "Rose PDF mutation support: disabled (edit/save/import/annotation headers missing)")
        endif()
    else()
        message(
            STATUS
            "Rose PDF support: disabled (optional PDFium package not found under "
            "${ROSE_PDFIUM_ROOT})"
        )
    endif()
endif()


# =============================================================================
# RoseMathSandbox
# =============================================================================

add_executable(
    RoseMathSandbox

    src/ui/MathSandbox.cpp
    src/ui/MathRenderer.cpp
    src/platform/SdlRuntime.cpp
)

target_include_directories(
    RoseMathSandbox
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    RoseMathSandbox
    PRIVATE
        SDL3::SDL3
        LaTeX
        gdiplus
)

rose_configure_cpp20(RoseMathSandbox)
rose_copy_runtime_dlls(RoseMathSandbox)


# =============================================================================
# RoseOcrSandbox
# =============================================================================
#
# Small dependency/behavior checkpoint. It proves Rose can locate the local
# Tesseract executable and OCR an image without loading llama.cpp or the full UI.

add_executable(
    RoseOcrSandbox

    src/ocr/OcrSandbox.cpp
    src/ocr/TesseractCliOcrEngine.cpp
)

target_include_directories(
    RoseOcrSandbox
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseOcrSandbox)
rose_enable_warnings(RoseOcrSandbox)


# =============================================================================
# RoseVisionWorker
# =============================================================================
#
# The worker owns its llama.cpp/libmtmd runtime in a separate process so Rose.exe
# does not host two competing backend lifetimes. Its VRAM is released when the
# helper process exits.

add_executable(
    RoseVisionWorker

    src/vision/VisionWorker.cpp
)

target_link_libraries(
    RoseVisionWorker
    PRIVATE
        llama
        mtmd
)

rose_configure_cpp20(RoseVisionWorker)
rose_enable_warnings(RoseVisionWorker)
rose_copy_runtime_dlls(RoseVisionWorker)


# =============================================================================
# RoseVisionSandbox
# =============================================================================
#
# Exercises the same process provider used by Rose without loading the desktop UI,
# persistence, OCR, or Rose's normal text model.

add_executable(
    RoseVisionSandbox

    src/vision/VisionSandbox.cpp
    src/vision/VisionTypes.h
    src/vision/IVisionProvider.h
    src/vision/LlamaMtmdVisionProvider.h
    src/vision/LlamaMtmdVisionProvider.cpp
)

target_include_directories(
    RoseVisionSandbox
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_dependencies(
    RoseVisionSandbox
    RoseVisionWorker
)

rose_configure_cpp20(RoseVisionSandbox)
rose_enable_warnings(RoseVisionSandbox)


# =============================================================================
# RoseAvatarSandbox
# =============================================================================
#
# Small graphical test application for avatar rendering independently from model
# inference and the main conversation frontend.

add_executable(
    RoseAvatarSandbox

    src/avatar/AvatarController.h
    src/avatar/AvatarController.cpp
    src/avatar/AvatarInteraction.h
    src/avatar/AvatarPreviewController.h
    src/avatar/AvatarPreviewController.cpp
    src/avatar/AvatarPresentationTimeline.h
    src/avatar/AvatarPresentationTimeline.cpp
    src/avatar/AvatarState.h
    src/avatar/IAvatar.h
    src/avatar/SdlAvatar.h
    src/avatar/SdlAvatar.cpp
    src/avatar/AvatarSandbox.cpp

    src/platform/SdlRuntime.h
    src/platform/SdlRuntime.cpp

    src/core/RoseActivity.h
)

target_include_directories(
    RoseAvatarSandbox
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    RoseAvatarSandbox
    PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
)

rose_configure_cpp20(RoseAvatarSandbox)
rose_enable_warnings(RoseAvatarSandbox)
rose_copy_runtime_dlls(RoseAvatarSandbox)


# =============================================================================
# Rose build/source identity
# =============================================================================
#
# This target runs on every Rose build so Git revision/dirty state describes the
# executable being produced, not merely the last CMake configure. The generator
# only updates the header when content changes, avoiding unnecessary recompiles.

set(
    ROSE_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

set(
    ROSE_BUILD_IDENTITY_HEADER
    "${ROSE_GENERATED_DIR}/RoseBuildIdentity.generated.h"
)

add_custom_target(
    RoseBuildIdentityGenerate

    COMMAND
        ${CMAKE_COMMAND}
        -DROSE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DROSE_OUTPUT_FILE=${ROSE_BUILD_IDENTITY_HEADER}
        -DROSE_PROJECT_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/GenerateRoseBuildIdentity.cmake

    BYPRODUCTS
        "${ROSE_BUILD_IDENTITY_HEADER}"

    COMMENT
        "Refreshing Rose build/source identity"

    VERBATIM
)

set_source_files_properties(
    "${ROSE_BUILD_IDENTITY_HEADER}"
    PROPERTIES
        GENERATED TRUE
)


function(rose_attach_build_identity target)
    target_include_directories(
        ${target}
        PRIVATE
            "${ROSE_GENERATED_DIR}"
    )

    target_compile_definitions(
        ${target}
        PRIVATE
            ROSE_BUILD_CONFIGURATION="$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Default>"
            ROSE_BUILD_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
            ROSE_BUILD_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    )

    add_dependencies(
        ${target}
        RoseBuildIdentityGenerate
    )
endfunction()


# =============================================================================
# Rose executable
# =============================================================================

add_executable(
    Rose

    src/main.cpp

    # -------------------------------------------------------------------------
    # Agent
    # -------------------------------------------------------------------------
    src/agent/AgentTypes.h
    src/agent/CapabilityRoutingGuard.h
    src/agent/CapabilityRoutingGuard.cpp
    src/agent/ToolSelectionAgent.h
    src/agent/ToolSelectionAgent.cpp
    src/agent/ToolObservation.h
    src/agent/ToolObservation.cpp
    src/agent/ToolCompletionAccumulator.h
    src/agent/ToolCompletionAccumulator.cpp
    src/agent/ToolConfirmation.h
    src/agent/ToolConfirmation.cpp
    src/agent/AgentLoop.h
    src/agent/AgentLoop.cpp
    src/agent/AgentJournal.h
    src/agent/AgentJournal.cpp
    src/agent/FileAgentJournalStore.h
    src/agent/FileAgentJournalStore.cpp
    src/agent/ToolExecutionService.h
    src/agent/ToolExecutionService.cpp

    # -------------------------------------------------------------------------
    # Core
    # -------------------------------------------------------------------------
    src/core/BuildIdentity.h
    src/core/BuildIdentity.cpp
    "${ROSE_BUILD_IDENTITY_HEADER}"
    src/core/RoseCore.h
    src/core/RoseCore.cpp

    # -------------------------------------------------------------------------
    # Content policy
    # -------------------------------------------------------------------------
    src/policy/ContentPolicy.h
    src/policy/ContentPolicy.cpp

    # -------------------------------------------------------------------------
    # Permissions
    # -------------------------------------------------------------------------
    src/permissions/PermissionSystem.h
    src/permissions/PermissionSystem.cpp
    src/permissions/ToolExecutionPolicy.h
    src/permissions/ToolExecutionPolicy.cpp

    # -------------------------------------------------------------------------
    # Optional integrations + secure credential boundary
    # -------------------------------------------------------------------------
    src/integrations/IntegrationTypes.h
    src/integrations/IntegrationTypes.cpp
    src/integrations/IIntegrationPermissionStore.h
    src/integrations/FileIntegrationPermissionStore.h
    src/integrations/FileIntegrationPermissionStore.cpp
    src/integrations/IntegrationPermissionRepository.h
    src/integrations/IntegrationPermissionRepository.cpp
    src/integrations/ICredentialVault.h
    src/integrations/WindowsCredentialVault.h
    src/integrations/WindowsCredentialVault.cpp
    src/integrations/IntegrationCommand.h
    src/integrations/IntegrationCommand.cpp
    src/integrations/HttpClient.h
    src/integrations/WinHttpClient.h
    src/integrations/WinHttpClient.cpp
    src/integrations/SimpleJson.h
    src/integrations/SimpleJson.cpp
    src/integrations/OutlookTypes.h
    src/integrations/OutlookConfiguration.h
    src/integrations/OutlookConfiguration.cpp
    src/integrations/OutlookClient.h
    src/integrations/OutlookClient.cpp
    src/integrations/OutlookIntegrationWorker.h
    src/integrations/OutlookIntegrationWorker.cpp
    src/integrations/OutlookCommand.h
    src/integrations/OutlookCommand.cpp

    # -------------------------------------------------------------------------
    # Logging
    # -------------------------------------------------------------------------
    src/logging/Logger.h
    src/logging/Logger.cpp

    # -------------------------------------------------------------------------
    # Avatar
    # -------------------------------------------------------------------------
    src/avatar/AvatarController.h
    src/avatar/AvatarController.cpp
    src/avatar/AvatarPresentationTimeline.h
    src/avatar/AvatarPresentationTimeline.cpp
    src/avatar/AvatarState.h
    src/avatar/ConsoleAvatar.h
    src/avatar/ConsoleAvatar.cpp
    src/avatar/IAvatar.h
    src/avatar/SdlAvatar.h
    src/avatar/SdlAvatar.cpp

    # Batch 19 - desktop avatar preview/demo control
    src/avatar/AvatarPreviewController.h
    src/avatar/AvatarPreviewController.cpp

    # -------------------------------------------------------------------------
    # Platform
    # -------------------------------------------------------------------------
    src/platform/SdlRuntime.h
    src/platform/SdlRuntime.cpp
    src/platform/TtfRuntime.h
    src/platform/TtfRuntime.cpp

    # -------------------------------------------------------------------------
    # Conversation + input
    # -------------------------------------------------------------------------
    src/conversation/Conversation.h
    src/conversation/Conversation.cpp
    src/input/UserSubmission.h

    # -------------------------------------------------------------------------
    # Model providers
    # -------------------------------------------------------------------------
    src/model/ModelTypes.h
    src/model/IModelProvider.h
    src/model/EchoModelProvider.h
    src/model/EchoModelProvider.cpp
    src/model/LlamaLogBridge.h
    src/model/LlamaLogBridge.cpp
    src/model/LlamaCppModelProvider.h
    src/model/LlamaCppModelProvider.cpp

    # -------------------------------------------------------------------------
    # Persistence
    # -------------------------------------------------------------------------
    src/persistence/IConversationStore.h
    src/persistence/FileConversationStore.h
    src/persistence/FileConversationStore.cpp
    src/persistence/DiscussionConversationStore.h
    src/persistence/DiscussionConversationStore.cpp

    # -------------------------------------------------------------------------
    # Projects + discussions workspace
    # -------------------------------------------------------------------------
    src/workspace/WorkspaceTypes.h
    src/workspace/IWorkspaceStore.h
    src/workspace/FileWorkspaceStore.h
    src/workspace/FileWorkspaceStore.cpp
    src/workspace/WorkspaceRepository.h
    src/workspace/WorkspaceRepository.cpp
    src/workspace/WorkspaceCommand.h
    src/workspace/WorkspaceCommand.cpp

    # -------------------------------------------------------------------------
    # Persistent background jobs / errands
    # -------------------------------------------------------------------------
    src/jobs/JobTypes.h
    src/jobs/IJobStore.h
    src/jobs/FileJobStore.h
    src/jobs/FileJobStore.cpp
    src/jobs/JobRepository.h
    src/jobs/JobRepository.cpp
    src/jobs/PersistentJobScheduler.h
    src/jobs/PersistentJobScheduler.cpp
    src/jobs/ErrandCommand.h
    src/jobs/ErrandCommand.cpp

    # -------------------------------------------------------------------------
    # File-format recognition + structured document extraction
    # -------------------------------------------------------------------------
    src/archives/ZipArchiveService.h
    src/archives/ZipArchiveService.cpp
    src/media/MediaService.h
    src/media/MediaService.cpp
    src/database/DatabaseService.h
    src/database/DatabaseService.cpp
    src/shortcuts/ShortcutService.h
    src/shortcuts/ShortcutService.cpp
    src/process/ProcessService.h
    src/process/ProcessService.cpp
    src/files/FileFormatCatalog.h
    src/files/FileFormatCatalog.cpp
    src/files/ProjectFileResolver.h
    src/files/ProjectFileResolver.cpp
    src/documents/OpenXmlDocumentExtractor.h
    src/documents/OpenXmlDocumentExtractor.cpp
    src/documents/OfficeDocumentMutationService.h
    src/documents/OfficeDocumentMutationService.cpp
    src/documents/PdfiumRuntime.h
    src/documents/PdfiumRuntime.cpp
    src/documents/PdfDocumentMutationService.h
    src/documents/PdfDocumentMutationService.cpp
    src/documents/ContextSafeDocumentSynthesizer.h
    src/documents/ContextSafeDocumentSynthesizer.cpp

    # -------------------------------------------------------------------------
    # Persistent project knowledge + format-reader registry
    # -------------------------------------------------------------------------
    src/knowledge/ProjectContentReader.h
    src/knowledge/ProjectContentReader.cpp
    src/knowledge/Utf8TextProjectContentReader.h
    src/knowledge/Utf8TextProjectContentReader.cpp
    src/knowledge/PdfProjectContentReader.h
    src/knowledge/PdfProjectContentReader.cpp
    src/knowledge/OpenXmlProjectContentReader.h
    src/knowledge/OpenXmlProjectContentReader.cpp
    src/knowledge/ImageProjectContentReader.h
    src/knowledge/ImageProjectContentReader.cpp
    src/knowledge/MediaProjectContentReader.h
    src/knowledge/MediaProjectContentReader.cpp
    src/knowledge/DatabaseProjectContentReader.h
    src/knowledge/DatabaseProjectContentReader.cpp
    src/knowledge/ShortcutProjectContentReader.h
    src/knowledge/ShortcutProjectContentReader.cpp
    src/knowledge/ZipArchiveProjectContentReader.h
    src/knowledge/ZipArchiveProjectContentReader.cpp
    src/knowledge/ProjectKnowledgeTypes.h
    src/knowledge/IProjectKnowledgeStore.h
    src/knowledge/FileProjectKnowledgeStore.h
    src/knowledge/FileProjectKnowledgeStore.cpp
    src/knowledge/ProjectKnowledgeRepository.h
    src/knowledge/ProjectKnowledgeRepository.cpp
    src/knowledge/ProjectKnowledgeIndexer.h
    src/knowledge/ProjectKnowledgeIndexer.cpp
    src/knowledge/ProjectKnowledgeWorker.h
    src/knowledge/ProjectKnowledgeWorker.cpp
    src/knowledge/ProjectKnowledgeCommand.h
    src/knowledge/ProjectKnowledgeCommand.cpp

    # -------------------------------------------------------------------------
    # Local long-term memory + retrieval
    # -------------------------------------------------------------------------
    src/memory/MemoryTypes.h
    src/memory/IMemoryStore.h
    src/memory/FileMemoryStore.h
    src/memory/FileMemoryStore.cpp
    src/memory/MemoryRepository.h
    src/memory/MemoryRepository.cpp
    src/memory/MemoryFactNormalizer.h
    src/memory/MemoryFactNormalizer.cpp
    src/memory/IMemoryRetriever.h
    src/memory/LexicalMemoryRetriever.h
    src/memory/LexicalMemoryRetriever.cpp
    src/memory/IMemoryObserver.h
    src/memory/ConversationMemoryCandidateTracker.h
    src/memory/ConversationMemoryCandidateTracker.cpp

    # -------------------------------------------------------------------------
    # OCR
    # -------------------------------------------------------------------------
    src/ocr/OcrTypes.h
    src/ocr/IOcrEngine.h
    src/ocr/TesseractCliOcrEngine.h
    src/ocr/TesseractCliOcrEngine.cpp

    # -------------------------------------------------------------------------
    # Vision
    # -------------------------------------------------------------------------
    src/vision/VisionTypes.h
    src/vision/IVisionProvider.h
    src/vision/LlamaMtmdVisionProvider.h
    src/vision/LlamaMtmdVisionProvider.cpp

    # -------------------------------------------------------------------------
    # Artifacts
    # -------------------------------------------------------------------------
    src/artifacts/Artifact.h
    src/artifacts/ArtifactStore.h
    src/artifacts/ArtifactStore.cpp

    # -------------------------------------------------------------------------
    # Local image generation
    # -------------------------------------------------------------------------
    src/imagegen/ImageGenerationTypes.h
    src/imagegen/ImageGenerationProfiles.h
    src/imagegen/ImageGenerationProfiles.cpp
    src/imagegen/LocalImageModelPreference.h
    src/imagegen/LocalImageModelPreference.cpp
    src/imagegen/LocalImageModelPreset.h
    src/imagegen/LocalImageModelPreset.cpp
    src/imagegen/IImageGenerator.h
    src/imagegen/StableDiffusionCliGenerator.h
    src/imagegen/StableDiffusionCliGenerator.cpp

    # -------------------------------------------------------------------------
    # Tools
    # -------------------------------------------------------------------------
    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/ToolRegistry.h
    src/tools/ToolRegistry.cpp

    src/tools/ReadFileTool.h
    src/tools/ReadFileTool.cpp
    src/tools/PdfTextExtractor.h
    src/tools/PdfTextExtractor.cpp
    src/tools/AttachmentIngestion.h
    src/tools/AttachmentIngestion.cpp

    src/tools/GenerateImageTool.h
    src/tools/GenerateImageTool.cpp
    src/tools/GenerateImageRegisteredTool.h
    src/tools/GenerateImageRegisteredTool.cpp

    src/tools/CreateTextFileTool.h
    src/tools/CreateTextFileTool.cpp
    src/tools/AnalyzeDirectoryDocumentsTool.h
    src/tools/AnalyzeDirectoryDocumentsTool.cpp
    src/tools/DocumentRenameMetadata.h
    src/tools/DocumentRenameMetadata.cpp
    src/tools/ContextualOcrRepair.h
    src/tools/ContextualOcrRepair.cpp
    src/tools/CourtFilingMetadataExtractor.h
    src/tools/CourtFilingMetadataExtractor.cpp
    src/tools/PlanDirectoryDocumentRenamesTool.h
    src/tools/PlanDirectoryDocumentRenamesTool.cpp
    src/tools/ApplyRenamePlanTool.h
    src/tools/ApplyRenamePlanTool.cpp
    src/tools/BatchMovePathsTool.h
    src/tools/BatchMovePathsTool.cpp
    src/tools/CreateDirectoryTool.h
    src/tools/CreateDirectoryTool.cpp
    src/tools/MovePathTool.h
    src/tools/MovePathTool.cpp
    src/tools/RecyclePathTool.h
    src/tools/RecyclePathTool.cpp
    src/tools/ListDirectoryTool.h
    src/tools/ListDirectoryTool.cpp
    src/tools/ScanDirectoryTreeTool.h
    src/tools/ScanDirectoryTreeTool.cpp
    src/tools/ListZipArchiveTool.h
    src/tools/ListZipArchiveTool.cpp
    src/tools/ExtractZipArchiveTool.h
    src/tools/ExtractZipArchiveTool.cpp
    src/tools/CreateZipArchiveTool.h
    src/tools/CreateZipArchiveTool.cpp
    src/tools/ReadTextFileRegisteredTool.h
    src/tools/ReadTextFileRegisteredTool.cpp
    src/tools/ReadPdfRegisteredTool.h
    src/tools/ReadPdfRegisteredTool.cpp
    src/tools/ReadOfficeDocumentRegisteredTool.h
    src/tools/ReadOfficeDocumentRegisteredTool.cpp
    src/tools/CreateOfficeDocumentTool.h
    src/tools/CreateOfficeDocumentTool.cpp
    src/tools/EditOfficeDocumentTool.h
    src/tools/EditOfficeDocumentTool.cpp
    src/tools/CreatePdfDocumentTool.h
    src/tools/CreatePdfDocumentTool.cpp
    src/tools/EditPdfDocumentTool.h
    src/tools/EditPdfDocumentTool.cpp
    src/tools/ExtractPdfPagesTool.h
    src/tools/ExtractPdfPagesTool.cpp
    src/tools/InspectImageRegisteredTool.h
    src/tools/InspectImageRegisteredTool.cpp
    src/tools/InspectMediaRegisteredTool.h
    src/tools/InspectMediaRegisteredTool.cpp
    src/tools/InspectDatabaseRegisteredTool.h
    src/tools/InspectDatabaseRegisteredTool.cpp
    src/tools/InspectShortcutRegisteredTool.h
    src/tools/InspectShortcutRegisteredTool.cpp
    src/tools/LaunchProgramTool.h
    src/tools/LaunchProgramTool.cpp
    src/tools/ListProcessesTool.h
    src/tools/ListProcessesTool.cpp
    src/tools/CloseProcessTool.h
    src/tools/CloseProcessTool.cpp
    src/tools/RememberMemoryTool.h
    src/tools/RememberMemoryTool.cpp

    # -------------------------------------------------------------------------
    # UI
    # -------------------------------------------------------------------------
    src/ui/ArtifactCardStack.h
    src/ui/ArtifactCardStack.cpp

    src/ui/ChatBridge.h
    src/ui/ChatBridge.cpp

    src/ui/ResponseDocument.h
    src/ui/ResponseParser.h
    src/ui/ResponseParser.cpp

    src/ui/MathRenderer.h
    src/ui/MathRenderer.cpp

    src/ui/RichResponseRenderer.h
    src/ui/RichResponseRenderer.cpp
    src/ui/RichTranscript.h
    src/ui/RichTranscript.cpp

    src/ui/RoseUiAction.h
    src/ui/SdlRoseContextMenu.h
    src/ui/SdlRoseContextMenu.cpp

    src/ui/SdlChatWindow.h
    src/ui/SdlChatWindow.cpp

    src/ui/TextPresentation.h
    src/ui/TextPresentation.cpp
    src/ui/InputRecallHistory.h
    src/ui/TextEditHistory.h
)

target_include_directories(
    Rose
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    Rose
    PRIVATE
        llama
        SDL3::SDL3
        SDL3_ttf::SDL3_ttf
        SDL3_image::SDL3_image
)

if(WIN32)
    # Rich-response math is a Windows UI implementation detail.
    target_link_libraries(
        Rose
        PRIVATE
            LaTeX
            gdiplus
            shell32
            advapi32
            winhttp
            mfplat
            mfreadwrite
            mfuuid
            windowscodecs
            ole32
            user32
    )
endif()

if(ROSE_HAS_PDFIUM)
    target_link_libraries(
        Rose
        PRIVATE
            PDFium::PDFium
    )

    target_compile_definitions(
        Rose
        PRIVATE
            ROSE_HAS_PDFIUM=1
    )
endif()

if(ROSE_HAS_PDFIUM_MUTATION)
    target_compile_definitions(
        Rose
        PRIVATE
            ROSE_HAS_PDFIUM_MUTATION=1
    )
endif()

# Rose invokes the semantic-vision worker by executable path at runtime rather
# than linking libmtmd into Rose.exe.
add_dependencies(
    Rose
    RoseVisionWorker
)

rose_configure_cpp20(Rose)
rose_enable_warnings(Rose)
rose_attach_build_identity(Rose)
rose_copy_runtime_dlls(Rose)


# =============================================================================
# Build/source identity unit test
# =============================================================================

add_executable(
    RoseBuildIdentityTest

    src/core/BuildIdentity.h
    src/core/BuildIdentity.cpp
    src/core/BuildIdentity_test.cpp
    "${ROSE_BUILD_IDENTITY_HEADER}"
)

target_include_directories(
    RoseBuildIdentityTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseBuildIdentityTest)
rose_enable_warnings(RoseBuildIdentityTest)
rose_attach_build_identity(RoseBuildIdentityTest)


# =============================================================================
# Context-safe large-document synthesis unit test
# =============================================================================

add_executable(
    RoseContextSafeDocumentSynthesizerTest

    src/documents/ContextSafeDocumentSynthesizer.h
    src/documents/ContextSafeDocumentSynthesizer.cpp
    src/documents/ContextSafeDocumentSynthesizer_test.cpp
    src/model/IModelProvider.h
    src/model/ModelTypes.h
)

target_include_directories(
    RoseContextSafeDocumentSynthesizerTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseContextSafeDocumentSynthesizerTest)
rose_enable_warnings(RoseContextSafeDocumentSynthesizerTest)


# =============================================================================
# Project-scoped file reference resolver unit test
# =============================================================================

add_executable(
    RoseProjectFileResolverTest

    src/files/ProjectFileResolver.h
    src/files/ProjectFileResolver.cpp
    src/files/ProjectFileResolver_test.cpp
)

target_include_directories(
    RoseProjectFileResolverTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseProjectFileResolverTest)
rose_enable_warnings(RoseProjectFileResolverTest)


# =============================================================================
# Tool-selection parser unit test
# =============================================================================
#
# Exercises the model-control protocol parser without loading llama.cpp. In
# particular, local models sometimes collapse ACTION=TOOL + TOOL=<id> into the
# unambiguous shorthand ACTION=<registered tool id>.

add_executable(
    RoseToolSelectionAgentTest

    src/agent/AgentTypes.h
    src/agent/CapabilityRoutingGuard.h
    src/agent/CapabilityRoutingGuard.cpp
    src/agent/ToolSelectionAgent.h
    src/agent/ToolSelectionAgent.cpp
    src/agent/ToolSelectionAgent_test.cpp
    src/files/FileFormatCatalog.h
    src/files/FileFormatCatalog.cpp
    src/logging/Logger.h
    src/logging/Logger.cpp
    src/model/IModelProvider.h
    src/model/ModelTypes.h
    src/tools/ITool.h
    src/tools/ToolRegistry.h
    src/tools/ToolRegistry.cpp
    src/tools/ToolTypes.h
)

target_include_directories(
    RoseToolSelectionAgentTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseToolSelectionAgentTest)
rose_enable_warnings(RoseToolSelectionAgentTest)


# =============================================================================
# Tool completion authority unit test
# =============================================================================

add_executable(
    RoseToolCompletionTest

    src/agent/ToolCompletionAccumulator.h
    src/agent/ToolCompletionAccumulator.cpp
    src/agent/ToolCompletionAccumulator_test.cpp
)

target_include_directories(
    RoseToolCompletionTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseToolCompletionTest)
rose_enable_warnings(RoseToolCompletionTest)


# =============================================================================
# Shared tool-execution boundary + persistent agent journal unit test
# =============================================================================

add_executable(
    RoseAgentExecutionTest

    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/ToolRegistry.h
    src/tools/ToolRegistry.cpp
    src/permissions/ToolExecutionPolicy.h
    src/permissions/ToolExecutionPolicy.cpp
    src/agent/AgentJournal.h
    src/agent/AgentJournal.cpp
    src/agent/FileAgentJournalStore.h
    src/agent/FileAgentJournalStore.cpp
    src/agent/ToolExecutionService.h
    src/agent/ToolExecutionService.cpp
    src/agent/AgentExecution_test.cpp
)

target_include_directories(
    RoseAgentExecutionTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseAgentExecutionTest)
rose_enable_warnings(RoseAgentExecutionTest)


# =============================================================================
# Image-generation profile unit test
# =============================================================================
#
# Dependency-light test for Rose's provider-neutral image intent/profile layer.

add_executable(
    RoseImageGenerationProfileTest

    src/imagegen/ImageGenerationProfiles.h
    src/imagegen/ImageGenerationProfiles.cpp
    src/imagegen/ImageGenerationProfiles_test.cpp
)

target_include_directories(
    RoseImageGenerationProfileTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseImageGenerationProfileTest)
rose_enable_warnings(RoseImageGenerationProfileTest)


# =============================================================================
# Local image-model preset unit test
# =============================================================================

add_executable(
    RoseLocalImageModelPresetTest

    src/imagegen/ImageGenerationProfiles.h
    src/imagegen/ImageGenerationProfiles.cpp
    src/imagegen/StableDiffusionCliGenerator.h
    src/imagegen/LocalImageModelPreference.h
    src/imagegen/LocalImageModelPreference.cpp
    src/imagegen/LocalImageModelPreset.h
    src/imagegen/LocalImageModelPreset.cpp
    src/imagegen/LocalImageModelPreset_test.cpp
)

target_include_directories(
    RoseLocalImageModelPresetTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseLocalImageModelPresetTest)
rose_enable_warnings(RoseLocalImageModelPresetTest)


# =============================================================================
# Local memory persistence + retrieval unit test
# =============================================================================

add_executable(
    RoseMemorySystemTest

    src/memory/MemoryTypes.h
    src/memory/IMemoryStore.h
    src/memory/FileMemoryStore.h
    src/memory/FileMemoryStore.cpp
    src/memory/MemoryRepository.h
    src/memory/MemoryRepository.cpp
    src/memory/MemoryFactNormalizer.h
    src/memory/MemoryFactNormalizer.cpp
    src/memory/IMemoryRetriever.h
    src/memory/LexicalMemoryRetriever.h
    src/memory/LexicalMemoryRetriever.cpp
    src/memory/IMemoryObserver.h
    src/memory/ConversationMemoryCandidateTracker.h
    src/memory/ConversationMemoryCandidateTracker.cpp
    src/memory/MemorySystem_test.cpp
)

target_include_directories(
    RoseMemorySystemTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseMemorySystemTest)
rose_enable_warnings(RoseMemorySystemTest)


# =============================================================================
# Project + discussion workspace persistence unit test
# =============================================================================

add_executable(
    RoseWorkspaceSystemTest

    src/persistence/IConversationStore.h
    src/persistence/FileConversationStore.h
    src/persistence/FileConversationStore.cpp
    src/persistence/DiscussionConversationStore.h
    src/persistence/DiscussionConversationStore.cpp
    src/workspace/WorkspaceTypes.h
    src/workspace/IWorkspaceStore.h
    src/workspace/FileWorkspaceStore.h
    src/workspace/FileWorkspaceStore.cpp
    src/workspace/WorkspaceRepository.h
    src/workspace/WorkspaceRepository.cpp
    src/workspace/WorkspaceCommand.h
    src/workspace/WorkspaceCommand.cpp
    src/workspace/WorkspaceSystem_test.cpp
)

target_include_directories(
    RoseWorkspaceSystemTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseWorkspaceSystemTest)
rose_enable_warnings(RoseWorkspaceSystemTest)


# =============================================================================
# Desktop workspace bridge unit test
# =============================================================================

add_executable(
    RoseChatBridgeWorkspaceTest

    src/artifacts/Artifact.h
    src/input/UserSubmission.h
    src/workspace/WorkspaceTypes.h
    src/ui/ChatBridge.h
    src/ui/ChatBridge.cpp
    src/ui/ChatBridge_test.cpp
)

target_include_directories(
    RoseChatBridgeWorkspaceTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseChatBridgeWorkspaceTest)
rose_enable_warnings(RoseChatBridgeWorkspaceTest)


# =============================================================================
# Persistent background jobs / errands unit test
# =============================================================================

add_executable(
    RoseBackgroundJobsTest

    src/workspace/WorkspaceTypes.h
    src/jobs/JobTypes.h
    src/jobs/IJobStore.h
    src/jobs/FileJobStore.h
    src/jobs/FileJobStore.cpp
    src/jobs/JobRepository.h
    src/jobs/JobRepository.cpp
    src/jobs/PersistentJobScheduler.h
    src/jobs/PersistentJobScheduler.cpp
    src/jobs/ErrandCommand.h
    src/jobs/ErrandCommand.cpp
    src/jobs/BackgroundJobs_test.cpp
)

target_include_directories(
    RoseBackgroundJobsTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseBackgroundJobsTest)
rose_enable_warnings(RoseBackgroundJobsTest)


# =============================================================================
# Persistent project knowledge indexing + retrieval unit test
# =============================================================================

add_executable(
    RoseProjectKnowledgeTest

    src/archives/ZipArchiveService.h
    src/archives/ZipArchiveService.cpp
    src/media/MediaService.h
    src/media/MediaService.cpp
    src/database/DatabaseService.h
    src/database/DatabaseService.cpp
    src/shortcuts/ShortcutService.h
    src/shortcuts/ShortcutService.cpp
    src/integrations/SimpleJson.h
    src/integrations/SimpleJson.cpp
    src/files/FileFormatCatalog.h
    src/files/FileFormatCatalog.cpp
    src/documents/OpenXmlDocumentExtractor.h
    src/documents/OpenXmlDocumentExtractor.cpp
    src/ocr/OcrTypes.h
    src/ocr/IOcrEngine.h
    src/ocr/TesseractCliOcrEngine.h
    src/ocr/TesseractCliOcrEngine.cpp
    src/tools/ReadFileTool.h
    src/tools/PdfTextExtractor.h
    src/tools/PdfTextExtractor.cpp
    src/documents/PdfiumRuntime.h
    src/documents/PdfiumRuntime.cpp
    src/knowledge/ProjectContentReader.h
    src/knowledge/ProjectContentReader.cpp
    src/knowledge/Utf8TextProjectContentReader.h
    src/knowledge/Utf8TextProjectContentReader.cpp
    src/knowledge/PdfProjectContentReader.h
    src/knowledge/PdfProjectContentReader.cpp
    src/knowledge/OpenXmlProjectContentReader.h
    src/knowledge/OpenXmlProjectContentReader.cpp
    src/knowledge/ImageProjectContentReader.h
    src/knowledge/ImageProjectContentReader.cpp
    src/knowledge/MediaProjectContentReader.h
    src/knowledge/MediaProjectContentReader.cpp
    src/knowledge/DatabaseProjectContentReader.h
    src/knowledge/DatabaseProjectContentReader.cpp
    src/knowledge/ShortcutProjectContentReader.h
    src/knowledge/ShortcutProjectContentReader.cpp
    src/knowledge/ZipArchiveProjectContentReader.h
    src/knowledge/ZipArchiveProjectContentReader.cpp
    src/knowledge/ProjectKnowledgeTypes.h
    src/knowledge/IProjectKnowledgeStore.h
    src/knowledge/FileProjectKnowledgeStore.h
    src/knowledge/FileProjectKnowledgeStore.cpp
    src/knowledge/ProjectKnowledgeRepository.h
    src/knowledge/ProjectKnowledgeRepository.cpp
    src/knowledge/ProjectKnowledgeIndexer.h
    src/knowledge/ProjectKnowledgeIndexer.cpp
    src/knowledge/ProjectKnowledgeCommand.h
    src/knowledge/ProjectKnowledgeCommand.cpp
    src/knowledge/ProjectKnowledge_test.cpp
)

target_include_directories(
    RoseProjectKnowledgeTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(WIN32)
    target_link_libraries(
        RoseProjectKnowledgeTest
        PRIVATE
            mfplat
            mfreadwrite
            mfuuid
            windowscodecs
            ole32
            shell32
    )
endif()

rose_configure_cpp20(RoseProjectKnowledgeTest)
rose_enable_warnings(RoseProjectKnowledgeTest)


# =============================================================================
# Optional integration permissions + credential-vault boundary unit test
# =============================================================================

add_executable(
    RoseIntegrationSystemTest

    src/integrations/IntegrationTypes.h
    src/integrations/IntegrationTypes.cpp
    src/integrations/IIntegrationPermissionStore.h
    src/integrations/FileIntegrationPermissionStore.h
    src/integrations/FileIntegrationPermissionStore.cpp
    src/integrations/IntegrationPermissionRepository.h
    src/integrations/IntegrationPermissionRepository.cpp
    src/integrations/ICredentialVault.h
    src/integrations/WindowsCredentialVault.h
    src/integrations/WindowsCredentialVault.cpp
    src/integrations/IntegrationCommand.h
    src/integrations/IntegrationCommand.cpp
    src/permissions/ToolExecutionPolicy.h
    src/permissions/ToolExecutionPolicy.cpp
    src/tools/ToolTypes.h
    src/integrations/IntegrationSystem_test.cpp
)

target_include_directories(
    RoseIntegrationSystemTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(WIN32)
    target_link_libraries(
        RoseIntegrationSystemTest
        PRIVATE
            advapi32
    )
endif()

rose_configure_cpp20(RoseIntegrationSystemTest)
rose_enable_warnings(RoseIntegrationSystemTest)


# =============================================================================
# Outlook read-only provider protocol + parsing unit test
# =============================================================================

add_executable(
    RoseOutlookIntegrationTest

    src/integrations/HttpClient.h
    src/integrations/ICredentialVault.h
    src/integrations/SimpleJson.h
    src/integrations/SimpleJson.cpp
    src/integrations/OutlookTypes.h
    src/integrations/OutlookConfiguration.h
    src/integrations/OutlookConfiguration.cpp
    src/integrations/OutlookClient.h
    src/integrations/OutlookClient.cpp
    src/integrations/OutlookCommand.h
    src/integrations/OutlookCommand.cpp
    src/integrations/OutlookIntegration_test.cpp
)

target_include_directories(
    RoseOutlookIntegrationTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseOutlookIntegrationTest)
rose_enable_warnings(RoseOutlookIntegrationTest)


# =============================================================================
# ZIP archive lifecycle unit test
# =============================================================================

add_executable(
    RoseZipArchiveToolsTest

    src/archives/ZipArchiveService.h
    src/archives/ZipArchiveService.cpp
    src/files/FileFormatCatalog.h
    src/files/FileFormatCatalog.cpp
    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/ListZipArchiveTool.h
    src/tools/ListZipArchiveTool.cpp
    src/tools/ExtractZipArchiveTool.h
    src/tools/ExtractZipArchiveTool.cpp
    src/tools/CreateZipArchiveTool.h
    src/tools/CreateZipArchiveTool.cpp
    src/tools/ZipArchiveTools_test.cpp
)

target_include_directories(
    RoseZipArchiveToolsTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseZipArchiveToolsTest)
rose_enable_warnings(RoseZipArchiveToolsTest)


# =============================================================================
# Local media inspection routing/vision unit test
# =============================================================================

add_executable(
    RoseMediaToolsTest

    src/files/FileFormatCatalog.h
    src/files/FileFormatCatalog.cpp
    src/media/MediaService.h
    src/permissions/PermissionSystem.h
    src/permissions/PermissionSystem.cpp
    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/InspectMediaRegisteredTool.h
    src/tools/InspectMediaRegisteredTool.cpp
    src/vision/VisionTypes.h
    src/vision/IVisionProvider.h
    src/tools/MediaTools_test.cpp
)

target_include_directories(
    RoseMediaToolsTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseMediaToolsTest)
rose_enable_warnings(RoseMediaToolsTest)




# =============================================================================
# Read-only database + shortcut inspection unit test
# =============================================================================

add_executable(
    RoseDatabaseShortcutToolsTest

    src/database/DatabaseService.h
    src/shortcuts/ShortcutService.h
    src/shortcuts/ShortcutService.cpp
    src/files/FileFormatCatalog.h
    src/files/FileFormatCatalog.cpp
    src/permissions/PermissionSystem.h
    src/permissions/PermissionSystem.cpp
    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/InspectDatabaseRegisteredTool.h
    src/tools/InspectDatabaseRegisteredTool.cpp
    src/tools/InspectShortcutRegisteredTool.h
    src/tools/InspectShortcutRegisteredTool.cpp
    src/tools/DatabaseShortcutTools_test.cpp
)

target_include_directories(
    RoseDatabaseShortcutToolsTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(WIN32)
    target_link_libraries(
        RoseDatabaseShortcutToolsTest
        PRIVATE
            ole32
            shell32
    )
endif()

rose_configure_cpp20(RoseDatabaseShortcutToolsTest)
rose_enable_warnings(RoseDatabaseShortcutToolsTest)

# =============================================================================
# Controlled Office Open XML mutation tool unit test
# =============================================================================

add_executable(
    RoseOfficeMutationToolsTest

    src/documents/OfficeDocumentMutationService.h
    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/CreateOfficeDocumentTool.h
    src/tools/CreateOfficeDocumentTool.cpp
    src/tools/EditOfficeDocumentTool.h
    src/tools/EditOfficeDocumentTool.cpp
    src/tools/OfficeMutationTools_test.cpp
)

target_include_directories(
    RoseOfficeMutationToolsTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseOfficeMutationToolsTest)
rose_enable_warnings(RoseOfficeMutationToolsTest)


# =============================================================================
# Controlled PDF mutation tool unit test
# =============================================================================

add_executable(
    RosePdfMutationToolsTest

    src/documents/PdfDocumentMutationService.h
    src/documents/PdfiumRuntime.h
    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/CreatePdfDocumentTool.h
    src/tools/CreatePdfDocumentTool.cpp
    src/tools/EditPdfDocumentTool.h
    src/tools/EditPdfDocumentTool.cpp
    src/tools/ExtractPdfPagesTool.h
    src/tools/ExtractPdfPagesTool.cpp
    src/tools/PdfMutationTools_test.cpp
)

target_include_directories(
    RosePdfMutationToolsTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RosePdfMutationToolsTest)
rose_enable_warnings(RosePdfMutationToolsTest)


# =============================================================================
# Controlled process launch/list/close unit test
# =============================================================================

add_executable(
    RoseProcessToolsTest

    src/process/ProcessService.h
    src/shortcuts/ShortcutService.h
    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/LaunchProgramTool.h
    src/tools/LaunchProgramTool.cpp
    src/tools/ListProcessesTool.h
    src/tools/ListProcessesTool.cpp
    src/tools/CloseProcessTool.h
    src/tools/CloseProcessTool.cpp
    src/tools/ProcessTools_test.cpp
)

target_include_directories(
    RoseProcessToolsTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseProcessToolsTest)
rose_enable_warnings(RoseProcessToolsTest)

# =============================================================================
# Filesystem organization + recursive inventory unit test
# =============================================================================

add_executable(
    RoseFilesystemToolsTest

    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/CreateDirectoryTool.h
    src/tools/CreateDirectoryTool.cpp
    src/tools/MovePathTool.h
    src/tools/MovePathTool.cpp
    src/tools/RecyclePathTool.h
    src/tools/RecyclePathTool.cpp
    src/tools/ScanDirectoryTreeTool.h
    src/tools/ScanDirectoryTreeTool.cpp
    src/tools/BatchMovePathsTool.h
    src/tools/BatchMovePathsTool.cpp
    src/tools/FilesystemTools_test.cpp
)

target_include_directories(
    RoseFilesystemToolsTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(WIN32)
    target_link_libraries(
        RoseFilesystemToolsTest
        PRIVATE
            shell32
    )
endif()

rose_configure_cpp20(RoseFilesystemToolsTest)
rose_enable_warnings(RoseFilesystemToolsTest)


# =============================================================================
# Bounded whole-directory document analysis unit test
# =============================================================================

add_executable(
    RoseDirectoryDocumentAnalysisTest

    src/ocr/OcrTypes.h
    src/ocr/IOcrEngine.h
    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/ReadFileTool.h
    src/tools/PdfTextExtractor.h
    src/tools/PdfTextExtractor.cpp
    src/documents/PdfiumRuntime.h
    src/documents/PdfiumRuntime.cpp
    src/tools/AnalyzeDirectoryDocumentsTool.h
    src/tools/AnalyzeDirectoryDocumentsTool.cpp
    src/tools/DirectoryDocumentAnalysis_test.cpp
)

target_include_directories(
    RoseDirectoryDocumentAnalysisTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseDirectoryDocumentAnalysisTest)
rose_enable_warnings(RoseDirectoryDocumentAnalysisTest)

# =============================================================================
# Context-safe directory rename planning unit test
# =============================================================================

add_executable(
    RoseDirectoryRenamePlanTest

    src/logging/Logger.h
    src/logging/Logger.cpp
    src/model/ModelTypes.h
    src/model/IModelProvider.h
    src/ocr/OcrTypes.h
    src/ocr/IOcrEngine.h
    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/ReadFileTool.h
    src/tools/PdfTextExtractor.h
    src/tools/PdfTextExtractor.cpp
    src/documents/PdfiumRuntime.h
    src/documents/PdfiumRuntime.cpp
    src/tools/AnalyzeDirectoryDocumentsTool.h
    src/tools/AnalyzeDirectoryDocumentsTool.cpp
    src/tools/BatchMovePathsTool.h
    src/tools/BatchMovePathsTool.cpp
    src/tools/DocumentRenameMetadata.h
    src/tools/DocumentRenameMetadata.cpp
    src/tools/ContextualOcrRepair.h
    src/tools/ContextualOcrRepair.cpp
    src/tools/CourtFilingMetadataExtractor.h
    src/tools/CourtFilingMetadataExtractor.cpp
    src/tools/PlanDirectoryDocumentRenamesTool.h
    src/tools/PlanDirectoryDocumentRenamesTool.cpp
    src/tools/ApplyRenamePlanTool.h
    src/tools/ApplyRenamePlanTool.cpp
    src/tools/DirectoryRenamePlan_test.cpp
)

target_include_directories(
    RoseDirectoryRenamePlanTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseDirectoryRenamePlanTest)
rose_enable_warnings(RoseDirectoryRenamePlanTest)

# =============================================================================
# Avatar presentation timeline unit test
# =============================================================================
#
# Dependency-free test for presentation-only posture/state sequencing. This catches
# regressions in sit/stand insertion and minimum visual dwell timing without
# requiring SDL, a GPU, or model dependencies.

add_executable(
    RoseAvatarPresentationTest

    src/avatar/AvatarState.h
    src/avatar/AvatarPresentationTimeline.h
    src/avatar/AvatarPresentationTimeline.cpp
    src/avatar/AvatarPresentationTimeline_test.cpp
)

target_include_directories(
    RoseAvatarPresentationTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseAvatarPresentationTest)
rose_enable_warnings(RoseAvatarPresentationTest)


# =============================================================================
# Content policy unit test
# =============================================================================
#
# Tiny dependency-free executable so the hard maturity boundary is compiled and
# exercised even when large optional model/external dependencies are absent.

add_executable(
    RoseContentPolicyTest

    src/policy/ContentPolicy.h
    src/policy/ContentPolicy.cpp
    src/policy/ContentPolicy_test.cpp
)

target_include_directories(
    RoseContentPolicyTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

rose_configure_cpp20(RoseContentPolicyTest)
rose_enable_warnings(RoseContentPolicyTest)


# =============================================================================
# Source-level regression contract
# =============================================================================
#
# This is intentionally lightweight. It catches the specific class of regression
# that caused newer SdlChatWindow checkpoints to overwrite older integration hooks.
# Runtime behavior still needs the smoke-test checklist in README_REGRESSION_BASELINE.md.

add_custom_target(
    RoseRegressionCheck

    COMMAND
        ${CMAKE_COMMAND}
        -DROSE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/CheckRoseSourceRegression.cmake

    COMMAND
        $<TARGET_FILE:RoseBuildIdentityTest>

    COMMAND
        $<TARGET_FILE:RoseContextSafeDocumentSynthesizerTest>

    COMMAND
        $<TARGET_FILE:RoseProjectFileResolverTest>

    COMMAND
        $<TARGET_FILE:RoseToolSelectionAgentTest>

    COMMAND
        $<TARGET_FILE:RoseToolCompletionTest>

    COMMAND
        $<TARGET_FILE:RoseAgentExecutionTest>

    COMMAND
        $<TARGET_FILE:RoseContentPolicyTest>

    COMMAND
        $<TARGET_FILE:RoseImageGenerationProfileTest>

    COMMAND
        $<TARGET_FILE:RoseLocalImageModelPresetTest>

    COMMAND
        $<TARGET_FILE:RoseMemorySystemTest>

    COMMAND
        $<TARGET_FILE:RoseWorkspaceSystemTest>

    COMMAND
        $<TARGET_FILE:RoseChatBridgeWorkspaceTest>

    COMMAND
        $<TARGET_FILE:RoseBackgroundJobsTest>

    COMMAND
        $<TARGET_FILE:RoseProjectKnowledgeTest>

    COMMAND
        $<TARGET_FILE:RoseIntegrationSystemTest>

    COMMAND
        $<TARGET_FILE:RoseOutlookIntegrationTest>

    COMMAND
        $<TARGET_FILE:RoseZipArchiveToolsTest>

    COMMAND
        $<TARGET_FILE:RoseMediaToolsTest>

    COMMAND
        $<TARGET_FILE:RoseDatabaseShortcutToolsTest>

    COMMAND
        $<TARGET_FILE:RoseProcessToolsTest>

    COMMAND
        $<TARGET_FILE:RoseOfficeMutationToolsTest>

    COMMAND
        $<TARGET_FILE:RosePdfMutationToolsTest>

    COMMAND
        $<TARGET_FILE:RoseFilesystemToolsTest>

    COMMAND
        $<TARGET_FILE:RoseDirectoryDocumentAnalysisTest>

    COMMAND
        $<TARGET_FILE:RoseAvatarPresentationTest>

    DEPENDS
        RoseBuildIdentityTest
        RoseContextSafeDocumentSynthesizerTest
        RoseProjectFileResolverTest
        RoseToolSelectionAgentTest
        RoseToolCompletionTest
        RoseAgentExecutionTest
        RoseContentPolicyTest
        RoseImageGenerationProfileTest
        RoseLocalImageModelPresetTest
        RoseMemorySystemTest
        RoseWorkspaceSystemTest
        RoseChatBridgeWorkspaceTest
        RoseBackgroundJobsTest
        RoseProjectKnowledgeTest
        RoseIntegrationSystemTest
        RoseOutlookIntegrationTest
        RoseZipArchiveToolsTest
        RoseMediaToolsTest
        RoseDatabaseShortcutToolsTest
        RoseProcessToolsTest
        RoseOfficeMutationToolsTest
        RosePdfMutationToolsTest
        RoseFilesystemToolsTest
        RoseDirectoryDocumentAnalysisTest
        RoseAvatarPresentationTest

    COMMENT
        "Checking Rose's cumulative source integration contract"

    VERBATIM
)

# Run the cheap source contract as part of ordinary Rose builds. This is not a
# runtime test suite; it is a tripwire against accidentally replacing a current
# integration module with an older checkpoint.
add_dependencies(
    Rose
    RoseRegressionCheck
)
