cmake_minimum_required(VERSION 3.16)

project(ved VERSION 0.0.0
    DESCRIPTION "Simple vim-like editor"
    LANGUAGES CXX)

### Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if(EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "You cannot build in a source directory (or any directory with a CMakeLists.txt file). Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
        STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Profile" "Release")
endif()

add_compile_options(
        -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
        -Woverloaded-virtual -Wpedantic -Wmisleading-indentation -Wnull-dereference
        -Wdouble-promotion -Wformat=2 -Wduplicated-cond -Wduplicated-branches -Wlogical-op
        -Wuseless-cast)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-s -O3)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Og -ggdb3 -fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
elseif(CMAKE_BUILD_TYPE STREQUAL "Profile")
    add_compile_options(-g -pg -Og)
endif()

add_subdirectory(extern/ncursespp)

add_subdirectory(src)

