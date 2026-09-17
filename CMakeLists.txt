# SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
#
# SPDX-License-Identifier: LGPL-2.1-only

cmake_minimum_required(VERSION 3.18)
project(ndrzcc LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

# Find required Flex and Bison packages
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

if(MSVC)
    add_compile_options(/W3 /WX /wd4244 /wd4267)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Enabling AddressSanitizer")
    add_definitions(-fsanitize=address -fno-omit-frame-pointer
                    -fno-optimize-sibling-calls -fstack-protector-all)
    add_link_options(-fsanitize=address)
endif()

if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-O3)
endif()

if(NOT MSVC)
    add_definitions(-g -Wall -Werror)
endif()

add_subdirectory(src)

if (NOT DISABLE_TESTS)
    add_subdirectory(tests)
endif()

# Export the compiler path to the parent project (chimera) when built as a
# subdirectory.
if (NOT "${CMAKE_SOURCE_DIR}" STREQUAL "${PROJECT_SOURCE_DIR}")
    set(NDRZCC ${NDRZCC} PARENT_SCOPE)
endif()

# Install man page
install(FILES man/ndrzcc.1 DESTINATION share/man/man1)
