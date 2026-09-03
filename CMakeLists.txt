cmake_minimum_required(VERSION 3.24)

project(
    Rose
    VERSION 0.1.0
    LANGUAGES CXX
)

# Rose targets C++20 throughout the project.
#
# Keeping this requirement at the project level makes the expected language
# version obvious, although individual target properties are still preferred
# when the project eventually contains third-party targets.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Rose
    src/main.cpp

    src/core/RoseCore.cpp

    src/model/EchoModelProvider.cpp
)

target_include_directories(Rose
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# MSVC gets a reasonably strict warning level from the start.
#
# We are NOT enabling /WX yet. Treating every warning as an error can become
# annoying while experimenting, particularly when external libraries arrive.
if(MSVC)
    target_compile_options(Rose PRIVATE /W4 /permissive-)
else()
    target_compile_options(Rose PRIVATE -Wall -Wextra -Wpedantic)
endif()