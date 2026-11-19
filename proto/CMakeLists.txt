add_library(devicescope_proto STATIC
    src/Version.cpp
)

target_include_directories(devicescope_proto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(devicescope_proto
    PUBLIC
        Qt6::Core
)

devicescope_set_warnings(devicescope_proto)