add_executable(devicescope_proto_tests
    test_placeholder.cpp
)

target_link_libraries(devicescope_proto_tests
    PRIVATE
        Qt6::Test
        devicescope_proto
)

devicescope_set_warnings(devicescope_proto_tests)

add_test(NAME proto_placeholder COMMAND devicescope_proto_tests)