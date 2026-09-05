cmake_minimum_required(VERSION 3.24)

project(
    Rose
    VERSION 0.1.0
    LANGUAGES CXX
)


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

add_subdirectory(external/llama.cpp)

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
   #src/avatar/AvatarSandbox.cpp
    src/avatar/AvatarState.h
    src/avatar/ConsoleAvatar.h
    src/avatar/ConsoleAvatar.cpp
    src/avatar/IAvatar.h
    src/avatar/SdlAvatar.h
    src/avatar/SdlAvatar.cpp

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