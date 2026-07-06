add_library(rdvc_protocol STATIC
    src/server_metrics.cpp
    src/status_parser.cpp
)

target_include_directories(rdvc_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

rdvc_configure_target(rdvc_protocol)
