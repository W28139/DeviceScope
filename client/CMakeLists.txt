add_executable(devicescope_client
    src/app/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
)

target_include_directories(devicescope_client
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(devicescope_client
    PRIVATE
        Qt6::Widgets
        devicescope_proto
)

if(WIN32)
    set_target_properties(devicescope_client PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

devicescope_set_warnings(devicescope_client)

set_target_properties(devicescope_client PROPERTIES
    OUTPUT_NAME devicescope-client
)