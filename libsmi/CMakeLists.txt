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