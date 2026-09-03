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


# =============================================================================
# Rose executable
# =============================================================================

add_executable(Rose
    src/main.cpp

    src/core/RoseCore.h
    src/core/RoseCore.cpp

    src/model/ModelTypes.h
    src/model/IModelProvider.h

    src/model/EchoModelProvider.h
    src/model/EchoModelProvider.cpp

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
)

# =============================================================================
# Runtime dependencies
# =============================================================================
#
# On Windows, llama.cpp builds as one or more DLLs.
#
# Linking Rose against the "llama" target tells the linker where to find the
# import library (*.lib), but Windows still needs the actual runtime DLLs when
# Rose.exe starts.
#
# CMake's TARGET_RUNTIME_DLLS generator expression collects the DLLs required
# by Rose's linked targets.
#
# After Rose is built, copy those DLLs beside Rose.exe:
#
#     build/Debug/
#         Rose.exe
#         llama.dll
#         ggml.dll
#         ggml-base.dll
#         ...
#
# Keeping runtime dependencies beside the executable means we do NOT need to:
#
#     - modify the user's system PATH
#     - install llama.cpp system-wide
#     - manually copy DLLs after every build
#
# COMMAND_EXPAND_LISTS is important because TARGET_RUNTIME_DLLS can produce
# multiple files.

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

endif()

# =============================================================================
# Compiler warnings
# =============================================================================
#
# These options apply ONLY to Rose.
#
# We deliberately do not impose Rose's warning policy on third-party code.
# External libraries frequently have their own warning policies and may produce
# warnings under /W4 that are irrelevant to Rose itself.

if(MSVC)

    target_compile_options(
        Rose
        PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
    )

else()

    target_compile_options(
        Rose
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

endif()
