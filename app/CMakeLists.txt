if(SNMPB_ENABLE_QWT)
    message(FATAL_ERROR
        "SNMPB_ENABLE_QWT requires a future Qt 6 port of bundled Qwt 6.1.2"
    )
endif()

set(SNMPB_APP_SOURCES
    agent.cpp
    agentrequestselection.cpp
    agentprofile.cpp
    agentprofileoperations.cpp
    agentprofilerepository.cpp
    agentprofileservice.cpp
    comboboxes.cpp
    discovery.cpp
    devicepane.cpp
    devicetree.cpp
    devicetreemodel.cpp
    devicetreerepository.cpp
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
    preferencesettings.cpp
    profilemetadatarepository.cpp
    profilemetadataservice.cpp
    profiletransfer.cpp
    snmprequestconfig.cpp
    snmprequestconfigadapter.cpp
    snmprequestcontext.cpp
    tablenodevalidation.cpp
    tabletraversal.cpp
    snmpb.cpp
    trap.cpp
    usmprofile.cpp
    main.cpp
)

set(SNMPB_APP_HEADERS
    agent.h
    agentrequestselection.h
    agentprofile.h
    agentprofileoperations.h
    agentprofilerepository.h
    agentprofileservice.h
    comboboxes.h
    discovery.h
    devicepane.h
    devicetree.h
    devicetreemodel.h
    devicetreerepository.h
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
    preferencesettings.h
    profilemetadatarepository.h
    profilemetadataservice.h
    profiletransfer.h
    snmprequestconfig.h
    snmprequestconfigadapter.h
    snmprequestcontext.h
    tablenodevalidation.h
    tabletraversal.h
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
    add_executable(snmpb_profile_transfer_test
        agentprofilerepository.cpp agentprofilerepository.h
        devicetreerepository.cpp devicetreerepository.h
        profilemetadatarepository.cpp profilemetadatarepository.h
        profiletransfer.cpp profiletransfer.h profiletransfer_test.cpp)
    target_link_libraries(snmpb_profile_transfer_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.profile-transfer COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_profile_transfer_test>)
    else()
        add_test(NAME snmpb.profile-transfer COMMAND
            $<TARGET_FILE:snmpb_profile_transfer_test>)
    endif()

    add_executable(snmpb_profile_metadata_test
        profilemetadatarepository.cpp profilemetadatarepository.h
        profilemetadataservice.cpp profilemetadataservice.h
        profilemetadata_test.cpp)
    set_target_properties(snmpb_profile_metadata_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_profile_metadata_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.profile-metadata COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_profile_metadata_test>)
    else()
        add_test(NAME snmpb.profile-metadata COMMAND
            $<TARGET_FILE:snmpb_profile_metadata_test>)
    endif()

    add_executable(snmpb_agentprofile_service_test
        agentprofileoperations.cpp
        agentprofileoperations.h
        agentprofilerepository.cpp
        agentprofilerepository.h
        agentprofileservice.cpp
        agentprofileservice.h
        agentprofileservice_test.cpp
    )
    set_target_properties(snmpb_agentprofile_service_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_agentprofile_service_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.agentprofile-service COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_agentprofile_service_test>)
    else()
        add_test(NAME snmpb.agentprofile-service COMMAND
            $<TARGET_FILE:snmpb_agentprofile_service_test>)
    endif()

    add_executable(snmpb_agentprofile_operations_test
        agentprofileoperations.cpp
        agentprofileoperations.h
        agentprofilerepository.cpp
        agentprofilerepository.h
        agentprofileoperations_test.cpp
    )
    target_link_libraries(snmpb_agentprofile_operations_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.agentprofile-operations COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_agentprofile_operations_test>)
    else()
        add_test(NAME snmpb.agentprofile-operations COMMAND
            $<TARGET_FILE:snmpb_agentprofile_operations_test>)
    endif()

    add_executable(snmpb_device_tree_repository_test
        agentprofilerepository.cpp
        agentprofilerepository.h
        devicetree.cpp
        devicetree.h
        devicetreerepository.cpp
        devicetreerepository.h
        devicetreerepository_test.cpp
    )
    target_link_libraries(snmpb_device_tree_repository_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.device-tree-repository COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_device_tree_repository_test>)
    else()
        add_test(NAME snmpb.device-tree-repository COMMAND
            $<TARGET_FILE:snmpb_device_tree_repository_test>)
    endif()

    add_executable(snmpb_device_tree_model_test
        agentprofilerepository.cpp
        agentprofilerepository.h
        devicetree.cpp
        devicetree.h
        devicetreemodel.cpp
        devicetreemodel.h
        devicetreerepository.cpp
        devicetreerepository.h
        devicetreemodel_test.cpp
    )
    set_target_properties(snmpb_device_tree_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_device_tree_model_test PRIVATE Qt6::Core Qt6::Widgets)
    if(WIN32)
        add_test(NAME snmpb.device-tree-model COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_device_tree_model_test>)
    else()
        add_test(NAME snmpb.device-tree-model
            COMMAND $<TARGET_FILE:snmpb_device_tree_model_test>)
    endif()

    add_executable(snmpb_device_pane_test
        agentprofilerepository.cpp
        agentprofilerepository.h
        devicepane.cpp
        devicepane.h
        devicetree.cpp
        devicetree.h
        devicetreemodel.cpp
        devicetreemodel.h
        devicetreerepository.cpp
        devicetreerepository.h
        devicepane_test.cpp
    )
    set_target_properties(snmpb_device_pane_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_device_pane_test PRIVATE Qt6::Core Qt6::Widgets)
    if(WIN32)
        add_test(NAME snmpb.device-pane COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen"
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_device_pane_test>)
    else()
        add_test(NAME snmpb.device-pane COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:snmpb_device_pane_test>)
    endif()

    add_executable(snmpb_table_traversal_test
        tabletraversal.cpp
        tabletraversal.h
        tabletraversal_test.cpp
    )
    target_link_libraries(snmpb_table_traversal_test
        PRIVATE Qt6::Core snmpb_libsmi snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.table-traversal COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_table_traversal_test>)
    else()
        add_test(NAME snmpb.table-traversal
            COMMAND $<TARGET_FILE:snmpb_table_traversal_test>)
    endif()

    add_executable(snmpb_table_node_validation_test
        tablenodevalidation.cpp
        tablenodevalidation.h
        tablenodevalidation_test.cpp
    )
    target_link_libraries(snmpb_table_node_validation_test PRIVATE snmpb_libsmi)
    add_test(NAME snmpb.table-node-validation
        COMMAND $<TARGET_FILE:snmpb_table_node_validation_test>)

    add_executable(snmpb_synchronous_config_test
        agentrequestselection.cpp
        agentrequestselection.h
        snmprequestconfig.cpp
        snmprequestconfig.h
        snmprequestconfigadapter.cpp
        snmprequestconfigadapter.h
        synchronousrequestconfig_test.cpp
    )
    target_link_libraries(snmpb_synchronous_config_test
        PRIVATE Qt6::Core snmpb_snmp_pp snmpb_libtomcrypt)

    if(WIN32)
        add_test(NAME snmpb.synchronous-config COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_synchronous_config_test>)
    else()
        add_test(NAME snmpb.synchronous-config
            COMMAND $<TARGET_FILE:snmpb_synchronous_config_test>)
    endif()

    add_executable(snmpb_agent_selection_test
        agentrequestselection.cpp
        agentrequestselection.h
        snmprequestconfig.cpp
        snmprequestconfig.h
        agentrequestselection_test.cpp
    )
    target_link_libraries(snmpb_agent_selection_test PRIVATE Qt6::Core)

    if(WIN32)
        add_test(NAME snmpb.agent-selection COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_agent_selection_test>)
    else()
        add_test(NAME snmpb.agent-selection
            COMMAND $<TARGET_FILE:snmpb_agent_selection_test>)
    endif()

    add_executable(snmpb_request_context_test
        snmprequestconfig.cpp
        snmprequestconfig.h
        snmprequestcontext.cpp
        snmprequestcontext.h
        snmprequestcontext_test.cpp
    )
    target_link_libraries(snmpb_request_context_test PRIVATE Qt6::Core)

    if(WIN32)
        add_test(NAME snmpb.request-context COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_request_context_test>)
    else()
        add_test(NAME snmpb.request-context
            COMMAND $<TARGET_FILE:snmpb_request_context_test>)
    endif()

    add_executable(snmpb_request_config_test
        snmprequestconfig.cpp
        snmprequestconfig.h
        snmprequestconfigadapter.cpp
        snmprequestconfigadapter.h
        snmprequestconfig_test.cpp
    )
    target_link_libraries(snmpb_request_config_test
        PRIVATE Qt6::Core snmpb_snmp_pp snmpb_libtomcrypt)

    if(WIN32)
        add_test(NAME snmpb.request-config COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_request_config_test>)
    else()
        add_test(NAME snmpb.request-config
            COMMAND $<TARGET_FILE:snmpb_request_config_test>)
    endif()

    add_executable(snmpb_preferences_settings_test
        preferencesettings.cpp
        preferencesettings.h
        preferencesettings_test.cpp
    )
    target_link_libraries(snmpb_preferences_settings_test PRIVATE Qt6::Core)

    if(WIN32)
        add_test(NAME snmpb.preferences-settings COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_preferences_settings_test>)
    else()
        add_test(NAME snmpb.preferences-settings
            COMMAND $<TARGET_FILE:snmpb_preferences_settings_test>)
    endif()

    add_executable(snmpb_agentprofile_repository_test
        agentprofilerepository.cpp
        agentprofilerepository.h
        agentprofilerepository_test.cpp
    )
    target_link_libraries(snmpb_agentprofile_repository_test PRIVATE Qt6::Core)

    if(WIN32)
        add_test(
            NAME snmpb.agentprofile-repository
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
                $<TARGET_FILE:snmpb_agentprofile_repository_test>
        )
    else()
        add_test(
            NAME snmpb.agentprofile-repository
            COMMAND $<TARGET_FILE:snmpb_agentprofile_repository_test>
        )
    endif()

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
