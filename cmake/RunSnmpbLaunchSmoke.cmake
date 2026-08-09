if(NOT DEFINED SNMPB_EXECUTABLE OR
   NOT DEFINED SNMPB_QT_BIN_DIR OR
   NOT DEFINED SNMPB_SOURCE_DIR OR
   NOT DEFINED SNMPB_SMOKE_DIR)
    message(FATAL_ERROR "SNMPB launch smoke test arguments are incomplete")
endif()

file(REMOVE_RECURSE "${SNMPB_SMOKE_DIR}")
set(settings_dir "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net")
file(MAKE_DIRECTORY "${settings_dir}")
set(ietf_mib_path "${SNMPB_SOURCE_DIR}/libsmi/mibs/ietf")
set(iana_mib_path "${SNMPB_SOURCE_DIR}/libsmi/mibs/iana")
file(WRITE "${settings_dir}/SnmpB.ini"
"[mibpaths]\n"
"1\\dir=${ietf_mib_path}\n"
"2\\dir=${iana_mib_path}\n"
"size=2\n\n"
"[mibpreloads]\n"
"1\\mib=SNMPv2-MIB\n"
"2\\mib=IF-MIB\n"
"size=2\n"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "QT_QPA_PLATFORM=offscreen"
        "PATH=${SNMPB_QT_BIN_DIR};$ENV{PATH}"
        "${SNMPB_EXECUTABLE}"
        --launch-smoke-test "${SNMPB_SMOKE_DIR}"
        --smoke-mib "${SNMPB_SOURCE_DIR}/libsmi/mibs/ietf/SNMPv2-MIB"
    WORKING_DIRECTORY "${SNMPB_SOURCE_DIR}/libsmi"
    RESULT_VARIABLE smoke_result
    OUTPUT_VARIABLE smoke_stdout
    ERROR_VARIABLE smoke_stderr
    TIMEOUT 45
)

message(STATUS "SnmpB launch smoke stdout:\n${smoke_stdout}")
message(STATUS "SnmpB launch smoke stderr:\n${smoke_stderr}")

set(smoke_log "${SNMPB_SMOKE_DIR}/launch-smoke.log")
if(EXISTS "${smoke_log}")
    file(READ "${smoke_log}" smoke_details)
    message(STATUS "SnmpB launch smoke checks:\n${smoke_details}")
endif()

if(NOT smoke_result EQUAL 0)
    message(FATAL_ERROR "SnmpB launch smoke failed with exit code ${smoke_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "QT_QPA_PLATFORM=offscreen"
        "PATH=${SNMPB_QT_BIN_DIR};$ENV{PATH}"
        "${SNMPB_EXECUTABLE}"
        --launch-smoke-test "${SNMPB_SMOKE_DIR}"
        --smoke-mib "${SNMPB_SOURCE_DIR}/libsmi/mibs/ietf/SNMPv2-MIB"
    WORKING_DIRECTORY "${SNMPB_SOURCE_DIR}/libsmi"
    RESULT_VARIABLE reload_result
    TIMEOUT 45
)

if(NOT reload_result EQUAL 0)
    message(FATAL_ERROR "SnmpB persistence reload failed with exit code ${reload_result}")
endif()

set(expected_files
    "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/SnmpB.ini"
    "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/agents.conf"
    "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/smi.conf"
    "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/boot_counter.conf"
    "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/log.conf"
)

foreach(expected_file IN LISTS expected_files)
    if(NOT EXISTS "${expected_file}")
        message(FATAL_ERROR "Expected isolated configuration file missing: ${expected_file}")
    endif()
endforeach()
