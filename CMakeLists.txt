cmake_minimum_required(VERSION 3.24)

project(
    Rose
    VERSION 0.1.0
    LANGUAGES CXX
)

# -----------------------------------------------------------------------------
# llama.cpp GPU acceleration
# -----------------------------------------------------------------------------
#
# Rose's primary development machine has an NVIDIA GPU, so enable llama.cpp's
# CUDA backend.
#
# This affects llama.cpp only. Rose's ModelProvider abstraction remains
# independent of CUDA, so a future CPU/local/cloud provider does not inherit
# an NVIDIA dependency at the architecture level.

set(GGML_CUDA ON CACHE BOOL "" FORCE)

# =============================================================================
# llama.cpp
# =============================================================================
#
# IMPORTANT:
#
# Do NOT globally set CMAKE_CXX_STANDARD here.
#
# Rose is a C++20 project, but third-party dependencies should be allowed to
# select their own language standards.
#
# Current llama.cpp deliberately builds its core library as C++17. Forcing
# C++20 onto it can change UTF-8 literal behavior under MSVC and cause char8_t
# compilation errors.
#
# Rose's C++20 requirement is applied specifically to the Rose target below.


# We only need llama.cpp as an inference library.
#
# Disable its standalone executables, examples, tests, and server to reduce
# compile time and Visual Studio project clutter.
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)

# Build llama.cpp's multimodal library without enabling its full tools suite.
# RoseVisionWorker links libmtmd directly in its own helper process.
set(LLAMA_BUILD_MTMD ON CACHE BOOL "" FORCE)

# This checkpoint handles still images only. Disabling mtmd video avoids adding
# ffmpeg/subprocess behavior to Rose before video is actually a product feature.
set(MTMD_VIDEO OFF CACHE BOOL "" FORCE)

add_subdirectory(external/llama.cpp
    EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------
# SDL3
# ------------------------------------------------------------------------------
#
# SDL is vendored as a pinned Git submodule, like llama.cpp.
#
# EXCLUDE_FROM_ALL prevents SDL's extra targets from becoming part of Rose's
# default build unless Rose actually depends on them.
add_subdirectory(
    external/SDL
    EXCLUDE_FROM_ALL
)

# -----------------------------------------------------------------------------
# SDL_ttf
# -----------------------------------------------------------------------------
#
# Rose vendors SDL_ttf so text rendering remains reproducible and does not
# depend on a machine-wide package installation.
#
# HarfBuzz remains enabled because proper text shaping will matter once the
# GUI grows beyond simple English text.
#
# PlutoSVG is unnecessary for the current chat UI. Disabling it avoids pulling
# in color-SVG/emoji rendering dependencies before Rose actually needs them.

set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)

add_subdirectory(
    external/SDL_ttf
    EXCLUDE_FROM_ALL
)

# -----------------------------------------------------------------------------
# SDL_image
# -----------------------------------------------------------------------------
#
# Rose currently uses SDL_image only for PNG avatar/UI textures.
#
# SDL_image enables many formats by default. Some, particularly AVIF, pull in
# large codec dependencies such as dav1d/AOM and may require external build
# tools such as NASM.
#
# Keep Rose's dependency surface deliberately small until another image format
# is actually needed.

set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)

set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)

# Formats Rose currently does not need.
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

# PNG is the only format required by Rose's avatar right now.
set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)

# SDL3 itself has a PNG loader, so SDL_image does not need to build and carry
# a separate vendored libpng + zlib stack for Rose's current requirements.
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_SAVE OFF CACHE BOOL "" FORCE)

add_subdirectory(
    external/SDL_image
    EXCLUDE_FROM_ALL
)

# =============================================================================
# MicroTeX - native LaTeX math layout
# =============================================================================

set(HAVE_LOG OFF CACHE BOOL "" FORCE)
set(GRAPHICS_DEBUG OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)

# =============================================================================
# MicroTeX
# =============================================================================
#
# MicroTeX uses `add_library(LaTeX "")` without specifying STATIC or SHARED.
#
# Rose's SDL dependencies enable BUILD_SHARED_LIBS, which would accidentally
# turn MicroTeX into a Windows DLL. MicroTeX is not configured as an exported
# Windows DLL, so no usable LaTeX.lib import library is produced.
#
# Temporarily force static-library behavior only while configuring MicroTeX,
# then restore Rose's previous BUILD_SHARED_LIBS value.

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
    set(
        BUILD_SHARED_LIBS
        "${ROSE_SAVED_BUILD_SHARED_LIBS}"
    )
else()
    unset(BUILD_SHARED_LIBS)
endif()


if(WIN32)

    # Do not propagate MicroTeX's _HAS_STD_BYTE=0 setting into Rose.
    set_property(
        TARGET LaTeX
        PROPERTY INTERFACE_COMPILE_DEFINITIONS
            BUILD_WIN32
    )


    # MicroTeX's core library does not add its Win32 graphics implementation
    # to the LaTeX target itself. Rose consumes the library directly, so add
    # that implementation here.
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

target_compile_features(
    RoseMathSandbox
    PRIVATE
        cxx_std_20
)

set_target_properties(
    RoseMathSandbox
    PROPERTIES
        CXX_EXTENSIONS OFF
)

target_link_libraries(
    RoseMathSandbox
    PRIVATE
        SDL3::SDL3
        LaTeX
        gdiplus
)


if(WIN32)

    add_custom_command(
        TARGET RoseMathSandbox
        POST_BUILD

        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:RoseMathSandbox>
            $<TARGET_FILE_DIR:RoseMathSandbox>

        COMMAND_EXPAND_LISTS
    )

endif()
# =============================================================================
# PDFium - native PDF text extraction
# =============================================================================
#
# Rose uses a pinned prebuilt PDFium package on Windows. Building PDFium from
# source requires Chromium's GN/depot_tools toolchain and is intentionally kept
# outside Rose's normal MSVC/CMake build.
#
# Expected layout after extracting pdfium-win-x64.tgz:
#
#     external/pdfium/include/fpdfview.h
#     external/pdfium/include/fpdf_text.h
#     external/pdfium/lib/pdfium.dll.lib
#     external/pdfium/bin/pdfium.dll

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
        message(FATAL_ERROR
            "Rose PDF support requires PDFium headers under "
            "${ROSE_PDFIUM_INCLUDE_DIR}. See README_PDF_SUPPORT.md."
        )
    endif()

    if(NOT EXISTS "${ROSE_PDFIUM_IMPORT_LIBRARY}")
        message(FATAL_ERROR
            "Rose PDF support requires ${ROSE_PDFIUM_IMPORT_LIBRARY}. "
            "See README_PDF_SUPPORT.md."
        )
    endif()

    if(NOT EXISTS "${ROSE_PDFIUM_RUNTIME_DLL}")
        message(FATAL_ERROR
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
            IMPORTED_IMPLIB
                "${ROSE_PDFIUM_IMPORT_LIBRARY}"
            IMPORTED_LOCATION
                "${ROSE_PDFIUM_RUNTIME_DLL}"
            INTERFACE_INCLUDE_DIRECTORIES
                "${ROSE_PDFIUM_INCLUDE_DIR}"
    )

endif()

# =============================================================================
# Rose OCR sandbox
# =============================================================================
#
# Small dependency/behavior checkpoint. It proves that Rose can locate the local
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

target_compile_features(
    RoseOcrSandbox
    PRIVATE
        cxx_std_20
)

set_target_properties(
    RoseOcrSandbox
    PROPERTIES
        CXX_EXTENSIONS OFF
)


# =============================================================================
# Rose semantic-vision worker
# =============================================================================
#
# The worker owns its own llama.cpp/libmtmd runtime in a separate process. This
# prevents Rose.exe's existing text-model BackendRuntime from competing with a
# second backend lifetime inside the same process. It also releases vision-model
# VRAM automatically when one image request finishes.

add_executable(
    RoseVisionWorker
        src/vision/VisionWorker.cpp
)

target_compile_features(
    RoseVisionWorker
    PRIVATE
        cxx_std_20
)

set_target_properties(
    RoseVisionWorker
    PROPERTIES
        CXX_EXTENSIONS OFF
)

target_link_libraries(
    RoseVisionWorker
    PRIVATE
        llama
        mtmd
)


# =============================================================================
# Rose semantic-vision sandbox
# =============================================================================
#
# Exercises the exact same process provider used by Rose without loading the
# desktop UI, persistence, OCR, or Rose's normal text model.

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

target_compile_features(
    RoseVisionSandbox
    PRIVATE
        cxx_std_20
)

set_target_properties(
    RoseVisionSandbox
    PROPERTIES
        CXX_EXTENSIONS OFF
)

add_dependencies(
    RoseVisionSandbox
    RoseVisionWorker
)


# =============================================================================
# Rose executable
# =============================================================================

add_executable(Rose
    src/main.cpp


    src/agent/AgentTypes.h
    src/agent/ToolSelectionAgent.h
    src/agent/ToolSelectionAgent.cpp
    src/agent/ToolObservation.h
    src/agent/ToolObservation.cpp

    src/permissions/ToolExecutionPolicy.h
    src/permissions/ToolExecutionPolicy.cpp
    src/core/RoseCore.h
    src/core/RoseCore.cpp

    src/logging/Logger.h
    src/logging/Logger.cpp
    
    src/avatar/AvatarController.h
    src/avatar/AvatarController.cpp
    src/avatar/AvatarState.h
    src/avatar/ConsoleAvatar.h
    src/avatar/ConsoleAvatar.cpp
    src/avatar/IAvatar.h
    src/avatar/SdlAvatar.h
    src/avatar/SdlAvatar.cpp

    src/platform/SdlRuntime.h
    src/platform/SdlRuntime.cpp
    src/platform/TtfRuntime.h
    src/platform/TtfRuntime.cpp

    src/conversation/Conversation.h
    src/conversation/Conversation.cpp

    src/model/ModelTypes.h
    src/model/IModelProvider.h

    src/model/EchoModelProvider.h
    src/model/EchoModelProvider.cpp

    src/model/LlamaLogBridge.h
    src/model/LlamaLogBridge.cpp

    src/model/LlamaCppModelProvider.h
    src/model/LlamaCppModelProvider.cpp

    src/persistence/IConversationStore.h
    src/persistence/FileConversationStore.h
    src/persistence/FileConversationStore.cpp

    src/input/UserSubmission.h

    src/ocr/OcrTypes.h
    src/ocr/IOcrEngine.h
    src/ocr/TesseractCliOcrEngine.h
    src/ocr/TesseractCliOcrEngine.cpp

    src/vision/VisionTypes.h
    src/vision/IVisionProvider.h
    src/vision/LlamaMtmdVisionProvider.h
    src/vision/LlamaMtmdVisionProvider.cpp

    src/permissions/PermissionSystem.h
    src/permissions/PermissionSystem.cpp

    src/tools/ReadFileTool.h
    src/tools/ReadFileTool.cpp
    src/tools/PdfTextExtractor.h
    src/tools/PdfTextExtractor.cpp
    src/tools/AttachmentIngestion.h
    src/tools/AttachmentIngestion.cpp

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

        # =========================================================================
    # Generated artifacts
    # =========================================================================

    src/artifacts/Artifact.h
    src/artifacts/ArtifactStore.h
    src/artifacts/ArtifactStore.cpp

    # =========================================================================
    # Local image generation
    # =========================================================================

    src/imagegen/ImageGenerationTypes.h
    src/imagegen/IImageGenerator.h
    src/imagegen/StableDiffusionCliGenerator.h
    src/imagegen/StableDiffusionCliGenerator.cpp

    # =========================================================================
    # Image generation tool
    # =========================================================================

    src/tools/GenerateImageTool.h
    src/tools/GenerateImageTool.cpp

    src/tools/ToolTypes.h
    src/tools/ITool.h
    src/tools/ToolRegistry.h
    src/tools/ToolRegistry.cpp
    src/tools/GenerateImageRegisteredTool.h
    src/tools/GenerateImageRegisteredTool.cpp
    src/agent/ToolConfirmation.h
    src/agent/ToolConfirmation.cpp

    src/tools/CreateTextFileTool.h
    src/tools/CreateTextFileTool.cpp
    src/agent/AgentLoop.h
    src/agent/AgentLoop.cpp
)

# =============================================================================
# Rose language standard
# =============================================================================
#
# Rose itself requires C++20.
#
# target_compile_features() applies the requirement specifically to Rose rather
# than globally forcing our chosen standard onto third-party dependencies.

target_compile_features(
    Rose
    PRIVATE
        cxx_std_20
)

# Disable compiler-specific language extensions for Rose.
#
# We want Rose to use portable C++20 wherever practical instead of accidentally
# depending on MSVC-only language behavior.
set_target_properties(
    Rose
    PROPERTIES
        CXX_EXTENSIONS OFF
)

# Rose invokes the semantic-vision helper by executable path at runtime. Make the
# worker an explicit build dependency without linking libmtmd into Rose.exe.
add_dependencies(
    Rose
    RoseVisionWorker
)

# ------------------------------------------------------------------------------
# Avatar sandbox
# ------------------------------------------------------------------------------
#
# Small graphical test application used to validate Rose's avatar rendering
# independently from model inference and the console frontend.
add_executable(RoseAvatarSandbox
    src/avatar/AvatarController.h
    src/avatar/AvatarController.cpp

    src/avatar/AvatarState.h
    src/avatar/IAvatar.h

    src/avatar/SdlAvatar.h
    src/avatar/SdlAvatar.cpp

    src/platform/SdlRuntime.h
    src/platform/SdlRuntime.cpp

    src/avatar/AvatarSandbox.cpp

    src/core/RoseActivity.h
)

target_compile_features(
    RoseAvatarSandbox
    PRIVATE
        cxx_std_20
)

set_target_properties(
    RoseAvatarSandbox
    PROPERTIES
        CXX_EXTENSIONS OFF
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

# =============================================================================
# Include directories
# =============================================================================

target_include_directories(
    Rose
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)


# =============================================================================
# Libraries
# =============================================================================
#
# Only LlamaCppModelProvider knows anything about llama.cpp, but the final Rose
# executable must still link against the llama library containing that
# implementation.

target_link_libraries(
    Rose
    PRIVATE
        llama
        SDL3::SDL3
        SDL3_ttf::SDL3_ttf
        SDL3_image::SDL3_image
)


if(WIN32)

    # Rich response math is an implementation detail of Rose's Windows UI.
    # MicroTeX is static, so its native GDI+ dependency is linked into Rose here.
    target_link_libraries(
        Rose
        PRIVATE
            LaTeX
            gdiplus
            PDFium::PDFium
    )

endif()

# =============================================================================
# Runtime dependencies
# =============================================================================
#
# On Windows, both llama.cpp and SDL may build runtime DLLs.
#
# TARGET_RUNTIME_DLLS asks CMake for the DLLs required by each executable and
# copies them beside that executable after a successful build.
#
# This keeps Rose self-contained without changing the user's PATH.

if(WIN32)

    add_custom_command(
        TARGET Rose
        POST_BUILD

        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:Rose>
            $<TARGET_FILE_DIR:Rose>

        COMMAND_EXPAND_LISTS
    )


    add_custom_command(
        TARGET RoseVisionWorker
        POST_BUILD

        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:RoseVisionWorker>
            $<TARGET_FILE_DIR:RoseVisionWorker>

        COMMAND_EXPAND_LISTS
    )


    add_custom_command(
        TARGET RoseAvatarSandbox
        POST_BUILD

        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:RoseAvatarSandbox>
            $<TARGET_FILE_DIR:RoseAvatarSandbox>

        COMMAND_EXPAND_LISTS
    )

endif()


# =============================================================================
# Compiler warnings
# =============================================================================
#
# These policies apply only to Rose-owned targets.
#
# Third-party dependencies such as llama.cpp and SDL retain their own compiler
# settings and warning policies.

if(MSVC)

    target_compile_options(
        Rose
        PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
    )


    target_compile_options(
        RoseAvatarSandbox
        PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
    )


    target_compile_options(
        RoseVisionWorker
        PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
    )


    target_compile_options(
        RoseVisionSandbox
        PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
    )


    target_compile_options(
        RoseOcrSandbox
        PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
    )

else()

    target_compile_options(
        Rose
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )


    target_compile_options(
        RoseAvatarSandbox
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )


    target_compile_options(
        RoseVisionWorker
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )


    target_compile_options(
        RoseVisionSandbox
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )


    target_compile_options(
        RoseOcrSandbox
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

endif()