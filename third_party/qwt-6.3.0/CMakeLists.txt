# SnmpB Next CMake integration for the unmodified Qwt 6.3.0 distribution.
# Only the base and 2-D plot sources required by Graphing v2 are compiled.
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Concurrent Svg)

file(GLOB QWT_630_SOURCES CONFIGURE_DEPENDS src/qwt*.cpp)
file(GLOB QWT_630_HEADERS CONFIGURE_DEPENDS src/qwt*.h)
list(FILTER QWT_630_SOURCES EXCLUDE REGEX "/qwt_(abstract_scale|abstract_slider|analog_clock|arrow_button|compass|compass_rose|counter|dial|dial_needle|knob|slider|thermo|wheel)\\.cpp$")
list(FILTER QWT_630_SOURCES EXCLUDE REGEX "/qwt_polar_.*\\.cpp$")
list(FILTER QWT_630_SOURCES EXCLUDE REGEX "/qwt_plot_(glcanvas|opengl_canvas|svgitem|renderer)\\.cpp$")
list(FILTER QWT_630_SOURCES EXCLUDE REGEX "/qwt_plot_(abstract_barchart|barchart|directpainter|graphicitem|histogram|intervalcurve|legenditem|magnifier|marker|multi_barchart|panner|picker|rasteritem|rescaler|scaleitem|shapeitem|spectrocurve|spectrogram|textlabel|tradingcurve|vectorfield|zoneitem|zoomer)\\.cpp$")
list(FILTER QWT_630_HEADERS EXCLUDE REGEX "/qwt_(abstract_scale|abstract_slider|analog_clock|arrow_button|compass|compass_rose|counter|dial|dial_needle|knob|slider|thermo|wheel)\\.h$")
list(FILTER QWT_630_HEADERS EXCLUDE REGEX "/qwt_polar_.*\\.h$")
list(FILTER QWT_630_HEADERS EXCLUDE REGEX "/qwt_plot_(glcanvas|opengl_canvas|svgitem|renderer)\\.h$")
list(FILTER QWT_630_HEADERS EXCLUDE REGEX "/qwt_plot_(abstract_barchart|barchart|directpainter|graphicitem|histogram|intervalcurve|legenditem|magnifier|marker|multi_barchart|panner|picker|rasteritem|rescaler|scaleitem|shapeitem|spectrocurve|spectrogram|textlabel|tradingcurve|vectorfield|zoneitem|zoomer)\\.h$")

add_library(snmpb_qwt STATIC ${QWT_630_SOURCES} ${QWT_630_HEADERS})
add_library(Qwt::Qwt ALIAS snmpb_qwt)
target_include_directories(snmpb_qwt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(snmpb_qwt PUBLIC QWT_STATIC)
target_link_libraries(snmpb_qwt PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Svg)
set_target_properties(snmpb_qwt PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    POSITION_INDEPENDENT_CODE ON)

install(FILES COPYING README DESTINATION ${CMAKE_INSTALL_DATADIR}/snmpb/licenses/qwt-6.3.0)
