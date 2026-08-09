cmake_minimum_required(VERSION 3.22)

project(SnmpBNext
    VERSION 1.0.0
    LANGUAGES C CXX
)

include(CTest)

option(SNMPB_ENABLE_QWT "Build SnmpB with bundled Qwt graphing support" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Widgets)

message(STATUS "Qt version: ${Qt6_VERSION}")
message(STATUS "SnmpB Next Qt/CMake environment configured successfully")

add_subdirectory(libsmi)
add_subdirectory(libtomcrypt)
add_subdirectory(snmp++)
add_subdirectory(app)
