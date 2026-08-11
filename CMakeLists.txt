cmake_minimum_required(VERSION 3.22)

project(SnmpBNext
    VERSION 1.0.0
    LANGUAGES C CXX
)

include(CTest)
include(GNUInstallDirs)

option(SNMPB_ENABLE_QWT "Build SnmpB with Qwt 6.3.0 graph presentation" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Widgets)

message(STATUS "Qt version: ${Qt6_VERSION}")
message(STATUS "SnmpB Next Qt/CMake environment configured successfully")

add_subdirectory(libsmi)
add_subdirectory(libtomcrypt)
add_subdirectory(snmp++)
if(SNMPB_ENABLE_QWT)
    add_subdirectory(third_party/qwt-6.3.0)
endif()
add_subdirectory(app)
