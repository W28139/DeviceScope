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

# 编解码测试：CRC、报文头、各消息体往返、边界值
add_executable(devicescope_codec_tests
    test_codec.cpp
)

target_link_libraries(devicescope_codec_tests
    PRIVATE
        Qt6::Test
        devicescope_proto
)

devicescope_set_warnings(devicescope_codec_tests)

add_test(NAME proto_codec COMMAND devicescope_codec_tests)

# 流式解包测试：半包、粘包、坏包重同步与上限
add_executable(devicescope_stream_tests
    test_stream.cpp
)

target_link_libraries(devicescope_stream_tests
    PRIVATE
        Qt6::Test
        devicescope_proto
)

devicescope_set_warnings(devicescope_stream_tests)

add_test(NAME proto_stream COMMAND devicescope_stream_tests)

# 模拟器测试：握手、心跳、命令、遥测速率、报警滞回
add_executable(devicescope_simulator_tests
    test_simulator.cpp
)

target_link_libraries(devicescope_simulator_tests
    PRIVATE
        Qt6::Test
        devicescope_simulator_core
)

devicescope_set_warnings(devicescope_simulator_tests)

add_test(NAME simulator COMMAND devicescope_simulator_tests)

# 客户端网络测试：连接/握手/断开循环与失败提示
add_executable(devicescope_client_tests
    test_client_network.cpp
)

target_link_libraries(devicescope_client_tests
    PRIVATE
        Qt6::Test
        devicescope_client_core
        devicescope_simulator_core
)

devicescope_set_warnings(devicescope_client_tests)

add_test(NAME client_network COMMAND devicescope_client_tests)

# 遥测有界队列测试：FIFO、满时丢最旧、批量取出与并发写入
add_executable(devicescope_telemetry_tests
    test_telemetry_queue.cpp
)

target_link_libraries(devicescope_telemetry_tests
    PRIVATE
        Qt6::Test
        devicescope_client_core
)

devicescope_set_warnings(devicescope_telemetry_tests)

add_test(NAME telemetry_queue COMMAND devicescope_telemetry_tests)