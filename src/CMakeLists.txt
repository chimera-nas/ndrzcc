# SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
#
# SPDX-License-Identifier: LGPL-2.1-only

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(FLEX_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/ndr.l)
set(FLEX_OUTPUT ${GENERATED_DIR}/lex.yy.c)

set(BISON_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/ndr.y)
set(BISON_OUTPUT ${GENERATED_DIR}/y.tab.c)
set(BISON_HEADER_OUTPUT ${GENERATED_DIR}/y.tab.h)

if(WIN32)
    set(FLEX_FLAGS "--wincompat")
endif()
FLEX_TARGET(lexer ${FLEX_INPUT} ${FLEX_OUTPUT} COMPILE_FLAGS "${FLEX_FLAGS}")
BISON_TARGET(parser ${BISON_INPUT} ${BISON_OUTPUT}
             DEFINES_FILE ${BISON_HEADER_OUTPUT})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${GENERATED_DIR})

add_flex_bison_dependency(lexer parser)

# Embed the NDR runtime (ndr_builtin.c/.h) as C string constants so the
# generated code is self-contained, exactly like xdrzcc does for xdr_builtin.
set(BUILTIN_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/ndr_builtin.c)
set(BUILTIN_HEADER ${CMAKE_CURRENT_BINARY_DIR}/ndr_builtin_h.c)

add_custom_command(
    OUTPUT ${BUILTIN_SOURCE}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/ndr_builtin.c
            -DOUTPUT=${BUILTIN_SOURCE} -DSYMBOL=embedded_builtin_c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_embedded.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ndr_builtin.c
            ${CMAKE_CURRENT_SOURCE_DIR}/generate_embedded.cmake
    VERBATIM
    COMMENT "Generating embedded C source"
)

add_custom_command(
    OUTPUT ${BUILTIN_HEADER}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/ndr_builtin.h
            -DOUTPUT=${BUILTIN_HEADER} -DSYMBOL=embedded_builtin_h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_embedded.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ndr_builtin.h
            ${CMAKE_CURRENT_SOURCE_DIR}/generate_embedded.cmake
    VERBATIM
    COMMENT "Generating embedded C header"
)

# Target name is unique (not just "generate_embedded_files") so it does not
# collide with xdrzcc's identically-purposed target when both are pulled into
# the chimera build via add_subdirectory.
add_custom_target(ndr_generate_embedded_files DEPENDS ${BUILTIN_SOURCE} ${BUILTIN_HEADER})

if(NOT MSVC)
    set_source_files_properties(${FLEX_OUTPUT} PROPERTIES COMPILE_OPTIONS -Wno-unused)
endif()

add_executable(ndrzcc
    ndrzcc.c
    ${BUILTIN_SOURCE}
    ${BUILTIN_HEADER}
    ${FLEX_lexer_OUTPUTS}
    ${BISON_parser_OUTPUT_SOURCE}
)

add_dependencies(ndrzcc ndr_generate_embedded_files)

set(NDRZCC "$<TARGET_FILE:ndrzcc>" PARENT_SCOPE)

install(TARGETS ndrzcc RUNTIME DESTINATION bin)
