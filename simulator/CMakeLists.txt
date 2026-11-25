# 设备模拟器：无界面的命令行程序，通过 TCP 模拟真实设备的行为
add_executable(devicescope_simulator
    src/main.cpp
    src/EchoServer.cpp
    src/EchoServer.h
)

# 头文件搜索路径：让源文件能直接 #include "EchoServer.h"
target_include_directories(devicescope_simulator
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 依赖 Qt Core、Qt Network（QTcpServer/QTcpSocket）和协议库
target_link_libraries(devicescope_simulator
    PRIVATE
        Qt6::Core
        Qt6::Network
        devicescope_proto
)

# 应用项目统一的编译警告选项
devicescope_set_warnings(devicescope_simulator)

# 生成的可执行文件名：devicescope-simulator
set_target_properties(devicescope_simulator PROPERTIES
    OUTPUT_NAME devicescope-simulator
)