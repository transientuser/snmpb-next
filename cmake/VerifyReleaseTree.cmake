if(NOT DEFINED SNMPB_PACKAGE_ROOT)
    message(FATAL_ERROR "SNMPB_PACKAGE_ROOT must name an installed release tree")
endif()

cmake_path(ABSOLUTE_PATH SNMPB_PACKAGE_ROOT NORMALIZE OUTPUT_VARIABLE root)

set(required_files
    "bin/snmpb.exe"
    "bin/qt.conf"
    "bin/mibs/SNMPv2-MIB"
    "bin/pibs/COPS-PR-SPPI"
    "plugins/platforms/qwindows.dll"
    "share/snmpb/licenses/SnmpB/COPYING"
    "share/snmpb/licenses/libsmi/COPYING"
    "share/snmpb/licenses/LibTomCrypt/LICENSE"
    "share/snmpb/licenses/SNMP++/LICENSE"
    "share/snmpb/licenses/qwt-6.3.0/COPYING"
    "share/snmpb/licenses/icons/open-iconic/LICENSE.txt"
    "share/snmpb/licenses/icons/devicons/LICENSE.txt"
    "share/snmpb/licenses/icons/feathericons/LICENSE.txt"
    "share/snmpb/licenses/icons/ionicons/LICENSE.txt"
    "portable-package-readme.txt"
    "release-candidate-validation.md"
    "manual-real-device-acceptance.md"
    "third-party-dependencies.md"
)

if(WIN32)
    list(APPEND required_files
        "bin/Qt6Core.dll"
        "bin/Qt6Gui.dll"
        "bin/Qt6Widgets.dll"
        "bin/Qt6Svg.dll"
    )
endif()

foreach(relative_path IN LISTS required_files)
    if(NOT EXISTS "${root}/${relative_path}")
        message(FATAL_ERROR "Required release file is missing: ${relative_path}")
    endif()
endforeach()

file(GLOB_RECURSE packaged_entries RELATIVE "${root}" "${root}/*")
set(private_names
    agents.conf device-tree.conf profile-metadata.conf graphs.conf
    credential-identities.conf community-credentials.conf
    credential-bindings.conf usm_users.conf log.conf SnmpB.ini
    boot_counter.conf smi.conf
)
foreach(relative_path IN LISTS packaged_entries)
    get_filename_component(name "${relative_path}" NAME)
    if(name IN_LIST private_names)
        message(FATAL_ERROR "Private runtime configuration was packaged: ${relative_path}")
    endif()
    if(relative_path MATCHES "(^|/)(CMakeFiles|_CPack_Packages)(/|$)" OR
       relative_path MATCHES "(CMakeCache\\.txt|\\.(obj|pdb|ilk|exp|lib))$")
        message(FATAL_ERROR "Build/development artifact was packaged: ${relative_path}")
    endif()
    if(relative_path MATCHES "(^|/)Qt6[^/]*d\\.dll$")
        message(FATAL_ERROR "Debug Qt runtime was packaged: ${relative_path}")
    endif()
endforeach()

file(GLOB mib_files "${root}/bin/mibs/*")
file(GLOB pib_files "${root}/bin/pibs/*")
list(LENGTH mib_files mib_count)
list(LENGTH pib_files pib_count)
if(mib_count LESS 1 OR pib_count LESS 1)
    message(FATAL_ERROR "Bundled MIB/PIB directories are empty")
endif()

message(STATUS "Verified release tree: ${root}")
message(STATUS "Bundled modules: ${mib_count} MIBs, ${pib_count} PIBs")
