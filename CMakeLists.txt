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
# PDFium
# =============================================================================
#
# Rose uses a pinned prebuilt PDFium package on Windows. Building PDFium from
# source requires Chromium's GN/depot_tools toolchain and remains outside Rose's
# normal CMake build.
#
# Expected layout:
#
#   external/pdfium/include/fpdfview.h
#   external/pdfium/include/fpdf_text.h
#   external/pdfium/lib/pdfium.dll.lib
#   external/pdfium/bin/pdfium.dll

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

    if(NOT EXISTS "${ROSE_PDFIUM_INCLUDE_DIR}/fpdfview.h")
        message(
            FATAL_ERROR
            "Rose PDF support requires PDFium headers under "
            "${ROSE_PDFIUM_INCLUDE_DIR}. See README_PDF_SUPPORT.md."
        )
    endif()

    if(NOT EXISTS "${ROSE_PDFIUM_IMPORT_LIBRARY}")
        message(
            FATAL_ERROR
            "Rose PDF support requires ${ROSE_PDFIUM_IMPORT_LIBRARY}. "
            "See README_PDF_SUPPORT.md."
        )
    endif()

    if(NOT EXISTS "${ROSE_PDFIUM_RUNTIME_DLL}")
        message(
            FATAL_ERROR
            "Rose PDF support requires ${ROSE_PDFIUM_RUNTIME_DLL}. "
            "See README_PDF_SUPPORT.md."
        )
    endif()

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
# Rose executable
# =============================================================================

add_executable(
    Rose

    src/main.cpp

    # -------------------------------------------------------------------------
    # Agent
    # -------------------------------------------------------------------------
    src/agent/AgentTypes.h
    src/agent/ToolSelectionAgent.h
    src/agent/ToolSelectionAgent.cpp
    src/agent/ToolObservation.h
    src/agent/ToolObservation.cpp
    src/agent/ToolConfirmation.h
    src/agent/ToolConfirmation.cpp
    src/agent/AgentLoop.h
    src/agent/AgentLoop.cpp
    src/agent/AgentJournal.h
    src/agent/AgentJournal.cpp

    # -------------------------------------------------------------------------
    # Core
    # -------------------------------------------------------------------------
    src/core/RoseCore.h
    src/core/RoseCore.cpp

    # -------------------------------------------------------------------------
    # Permissions
    # -------------------------------------------------------------------------
    src/permissions/PermissionSystem.h
    src/permissions/PermissionSystem.cpp
    src/permissions/ToolExecutionPolicy.h
    src/permissions/ToolExecutionPolicy.cpp

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
    src/avatar/AvatarState.h
    src/avatar/ConsoleAvatar.h
    src/avatar/ConsoleAvatar.cpp
    src/avatar/IAvatar.h
    src/avatar/SdlAvatar.h
    src/avatar/SdlAvatar.cpp

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
    src/tools/ListDirectoryTool.h
    src/tools/ListDirectoryTool.cpp
    src/tools/ReadTextFileRegisteredTool.h
    src/tools/ReadTextFileRegisteredTool.cpp

    # -------------------------------------------------------------------------
    # UI
    # -------------------------------------------------------------------------
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
    # Rich-response math and native PDF extraction are Windows UI implementation
    # details. MicroTeX is static; PDFium is the imported prebuilt DLL above.
    target_link_libraries(
        Rose
        PRIVATE
            LaTeX
            gdiplus
            PDFium::PDFium
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
rose_copy_runtime_dlls(Rose)
