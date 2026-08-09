add_library(snmpb_libsmi STATIC
    win/win.c

    lib/common.c
    lib/error.c
    lib/util.c
    lib/snprintf.c

    lib/smi.c
    lib/smi-data.c
    lib/smi-check.c

    lib/parser-smi.c
    lib/scanner-smi.c
)

target_include_directories(snmpb_libsmi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/lib

    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/win
)

target_compile_definitions(snmpb_libsmi
    PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        YY_NO_UNISTD_H
        YYMALLOC=malloc
        YYFREE=free
)

set_source_files_properties(
    lib/scanner-smi.c
    PROPERTIES
        COMPILE_DEFINITIONS "_Win32;__STDC__;YY_NEVER_INTERACTIVE"
)

# Build libsmi's native MIB validation utility.
# This gives SnmpB Next a regression tool for verifying that the
# patched MIB parser continues to behave correctly during modernization.

add_executable(snmpb_smilint
    tools/smilint.c
    tools/shhopt.c
)

target_include_directories(snmpb_smilint
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/win
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
)

target_compile_definitions(snmpb_smilint
    PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
)

target_link_libraries(snmpb_smilint
    PRIVATE
        snmpb_libsmi
)

if(BUILD_TESTING)

    set(SMILINT_REGRESSION_MIBS
        SNMPv2-MIB
        IF-MIB
        MAU-MIB
        RMON2-MIB
        BRIDGE-MIB
    )

    foreach(MIB IN LISTS SMILINT_REGRESSION_MIBS)

        add_test(
            NAME "libsmi.smilint.${MIB}"

            COMMAND
                ${CMAKE_COMMAND}
                "-DSMILINT=$<TARGET_FILE:snmpb_smilint>"
                "-DLIBSMI_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DMIB=${MIB}"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunSmilintRegression.cmake"
        )

    endforeach()

endif()