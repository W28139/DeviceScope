# 协议单元测试：基于 Qt Test，目前只校验协议常量
add_executable(devicescope_proto_tests
    test_placeholder.cpp
)

# 链接 Qt Test 框架和被测试的协议库
target_link_libraries(devicescope_proto_tests
    PRIVATE
        Qt6::Test
        devicescope_proto
)

# 应用项目统一的编译警告选项
devicescope_set_warnings(devicescope_proto_tests)

# 注册到 CTest：可用 ctest --preset linux-debug 运行
add_test(NAME proto_placeholder COMMAND devicescope_proto_tests)