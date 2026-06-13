# SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
#
# SPDX-License-Identifier: LGPL-2.1-only

add_definitions(-UNDEBUG)

macro(unit_test_ndrzcc name idl_file c_file)

    set(NDR_C ${CMAKE_CURRENT_BINARY_DIR}/${name}_ndr.c)
    set(NDR_H ${CMAKE_CURRENT_BINARY_DIR}/${name}_ndr.h)
    set(NDR_IDL ${CMAKE_CURRENT_SOURCE_DIR}/${idl_file})

    add_custom_command(
        OUTPUT ${NDR_C} ${NDR_H}
        COMMAND ${NDRZCC} ${NDR_IDL} ${NDR_C} ${NDR_H}
        DEPENDS ${NDR_IDL} ${NDRZCC}
        COMMENT "Compiling ${idl_file}"
    )

    include_directories(${CMAKE_CURRENT_BINARY_DIR})
    add_executable(${name} ${c_file} ${NDR_C})

    set_source_files_properties(
        ${NDR_C} PROPERTIES COMPILE_OPTIONS "-Wno-unused;-Wno-format-truncation"
    )

    add_dependencies(${name} ndrzcc)

    add_test(NAME ndrzcc/${name} COMMAND ${name})
    set_tests_properties(ndrzcc/${name} PROPERTIES LABELS "ndrzcc")

endmacro()

# Argument-validation failures
add_test(NAME ndrzcc/no_args COMMAND ${NDRZCC})
set_tests_properties(ndrzcc/no_args PROPERTIES WILL_FAIL TRUE)

add_test(NAME ndrzcc/missing_source COMMAND ${NDRZCC} no_such.idl out.c out.h)
set_tests_properties(ndrzcc/missing_source PROPERTIES WILL_FAIL TRUE)

unit_test_ndrzcc(sid sid.idl sid.c)
unit_test_ndrzcc(roundtrip roundtrip.idl roundtrip.c)
