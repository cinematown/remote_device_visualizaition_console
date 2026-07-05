cmake_minimum_required(VERSION 3.22)

project(
    RemoteDeviceVisualizationConsole
    VERSION 0.1.0
    LANGUAGES CXX
)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network OpenGLWidgets)

find_path(MOSQUITTO_INCLUDE_DIR mosquitto.h)
find_library(MOSQUITTO_LIBRARY mosquitto)

function(rdvc_configure_target target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_20)

    set_target_properties(
        ${target_name}
        PROPERTIES
            CXX_EXTENSIONS OFF
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_subdirectory(libs/protocol)
add_subdirectory(libs/common)

add_executable(rdvc_smoke src/main.cpp)
rdvc_configure_target(rdvc_smoke)

add_executable(rdvc_server
    apps/server/main.cpp
    apps/server/mqtt_bridge.cpp
    apps/server/mqtt_bridge.hpp
)
rdvc_configure_target(rdvc_server)
target_link_libraries(rdvc_server PRIVATE rdvc_common rdvc_protocol)

if(MOSQUITTO_INCLUDE_DIR AND MOSQUITTO_LIBRARY)
    target_compile_definitions(rdvc_server PRIVATE RDVC_HAS_MQTT=1)
    target_include_directories(rdvc_server PRIVATE ${MOSQUITTO_INCLUDE_DIR})
    target_link_libraries(rdvc_server PRIVATE ${MOSQUITTO_LIBRARY})
    message(STATUS "MQTT bridge enabled with libmosquitto")
else()
    target_compile_definitions(rdvc_server PRIVATE RDVC_HAS_MQTT=0)
    message(STATUS "MQTT bridge disabled: libmosquitto not found")
endif()

add_executable(rdvc_simulator apps/simulator/main.cpp)
rdvc_configure_target(rdvc_simulator)


add_executable(rdvc_loadgen apps/loadgen/main.cpp)
rdvc_configure_target(rdvc_loadgen)

add_executable(rdvc_viewer
    apps/viewer/main.cpp
    apps/viewer/device_viewport_widget.cpp
    apps/viewer/device_viewport_widget.hpp
    apps/viewer/network_worker.cpp
    apps/viewer/network_worker.hpp
)
rdvc_configure_target(rdvc_viewer)
set_target_properties(rdvc_viewer PROPERTIES AUTOMOC ON)
target_link_libraries(
    rdvc_viewer
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::OpenGLWidgets
        Qt${QT_VERSION_MAJOR}::Widgets
        rdvc_protocol
)
