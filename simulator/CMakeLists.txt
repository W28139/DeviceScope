add_executable(devicescope_simulator
    src/main.cpp
    src/EchoServer.cpp
    src/EchoServer.h
)

target_include_directories(devicescope_simulator
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(devicescope_simulator
    PRIVATE
        Qt6::Core
        Qt6::Network
        devicescope_proto
)

devicescope_set_warnings(devicescope_simulator)

set_target_properties(devicescope_simulator PROPERTIES
    OUTPUT_NAME devicescope-simulator
)