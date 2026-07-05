add_library(rdvc_common STATIC
    src/device_status_store.cpp
)

target_include_directories(rdvc_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rdvc_common
    PUBLIC
        rdvc_protocol
)

rdvc_configure_target(rdvc_common)
