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
# Rose executable
# =============================================================================

add_executable(Rose
    src/main.cpp

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

    src/ui/ChatBridge.h
    src/ui/ChatBridge.cpp
    src/ui/SdlChatWindow.h
    src/ui/SdlChatWindow.cpp
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

endif()