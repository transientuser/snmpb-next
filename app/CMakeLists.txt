if(SNMPB_ENABLE_QWT)
    message(FATAL_ERROR
        "SNMPB_ENABLE_QWT requires a future Qt 6 port of bundled Qwt 6.1.2"
    )
endif()

set(SNMPB_APP_SOURCES
    agent.cpp
    agentprofile.cpp
    comboboxes.cpp
    discovery.cpp
    logsnmpb.cpp
    markerwidget.cpp
    mibeditor.cpp
    mibhighlighter.cpp
    mibmodule.cpp
    mibnode.cpp
    mibselection.cpp
    mibtextedit.cpp
    mibview.cpp
    preferences.cpp
    snmpb.cpp
    trap.cpp
    usmprofile.cpp
    main.cpp
)

set(SNMPB_APP_HEADERS
    agent.h
    agentprofile.h
    comboboxes.h
    discovery.h
    logsnmpb.h
    markerwidget.h
    mibeditor.h
    mibhighlighter.h
    mibmodule.h
    mibnode.h
    mibselection.h
    mibtextedit.h
    mibview.h
    preferences.h
    snmpb.h
    snmpbapp.h
    trap.h
    usmprofile.h
)

set(SNMPB_APP_FORMS
    agentprofile.ui
    find.ui
    gotoline.ui
    mainw.ui
    plot.ui
    preferences.ui
    replace.ui
    usmprofile.ui
    varbinds.ui
)

if(WIN32)
    list(APPEND SNMPB_APP_SOURCES snmpb.rc)
endif()

add_executable(snmpb
    ${SNMPB_APP_SOURCES}
    ${SNMPB_APP_HEADERS}
    ${SNMPB_APP_FORMS}
    snmpb.qrc
)

set_target_properties(snmpb PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

target_compile_definitions(snmpb
    PRIVATE
        SNMPB_ENABLE_QWT=0
)

if(UNIX AND NOT APPLE)
    target_compile_definitions(snmpb
        PRIVATE
            SNMPB_INSTALL_DATA_RELATIVE_PATH="../${CMAKE_INSTALL_DATADIR}/snmpb"
    )
endif()

target_include_directories(snmpb
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(snmpb
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        snmpb_libsmi
        snmpb_snmp_pp
        snmpb_libtomcrypt
)

find_package(Qt6 6.11 REQUIRED COMPONENTS LinguistTools)

qt_add_translations(snmpb
    TS_FILES
        l10n/snmpb.uk_UA.ts
    LRELEASE_OPTIONS
        -removeidentical
        -compress
    RESOURCE_PREFIX
        /i18n
)

add_custom_command(TARGET snmpb POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSNMPB_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DSNMPB_RUNTIME_DATA_DIR=$<TARGET_FILE_DIR:snmpb>
        -P ${PROJECT_SOURCE_DIR}/cmake/StageSnmpbMibs.cmake
    COMMENT "Staging bundled MIBs and PIBs beside SnmpB"
    VERBATIM
)

file(GLOB SNMPB_BUNDLED_MIB_FILES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/libsmi/mibs/iana/*
    ${PROJECT_SOURCE_DIR}/libsmi/mibs/ietf/*
    ${PROJECT_SOURCE_DIR}/libsmi/mibs/tubs/*
)
file(GLOB SNMPB_BUNDLED_PIB_FILES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/libsmi/pibs/ietf/*
    ${PROJECT_SOURCE_DIR}/libsmi/pibs/tubs/*
)
list(FILTER SNMPB_BUNDLED_MIB_FILES EXCLUDE REGEX "/Makefile[^/]*$")
list(FILTER SNMPB_BUNDLED_MIB_FILES EXCLUDE REGEX "-orig$")
list(FILTER SNMPB_BUNDLED_PIB_FILES EXCLUDE REGEX "/Makefile[^/]*$")
list(FILTER SNMPB_BUNDLED_PIB_FILES EXCLUDE REGEX "-orig$")

install(TARGETS snmpb
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

if(WIN32)
    set(SNMPB_INSTALL_DATA_DIR ${CMAKE_INSTALL_BINDIR})
elseif(APPLE)
    set(SNMPB_INSTALL_DATA_DIR "snmpb.app/Contents/Resources")
else()
    set(SNMPB_INSTALL_DATA_DIR "${CMAKE_INSTALL_DATADIR}/snmpb")
endif()

install(FILES ${SNMPB_BUNDLED_MIB_FILES}
    DESTINATION "${SNMPB_INSTALL_DATA_DIR}/mibs"
)
install(FILES ${SNMPB_BUNDLED_PIB_FILES}
    DESTINATION "${SNMPB_INSTALL_DATA_DIR}/pibs"
)

if(BUILD_TESTING)
    add_test(
        NAME snmpb.launch-smoke
        COMMAND ${CMAKE_COMMAND}
            -DSNMPB_EXECUTABLE=$<TARGET_FILE:snmpb>
            -DSNMPB_QT_BIN_DIR=$<TARGET_FILE_DIR:Qt6::Core>
            -DSNMPB_RUNTIME_DATA_DIR=$<TARGET_FILE_DIR:snmpb>
            -DSNMPB_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DSNMPB_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/launch-smoke-config
            -P ${PROJECT_SOURCE_DIR}/cmake/RunSnmpbLaunchSmoke.cmake
    )
    set_tests_properties(snmpb.launch-smoke PROPERTIES TIMEOUT 60)
endif()
