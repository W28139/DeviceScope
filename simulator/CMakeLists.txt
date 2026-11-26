# 模拟器核心库：服务器、会话与设备模型，供可执行程序和测试共用
add_library(devicescope_simulator_core STATIC
    src/DeviceModel.cpp
    src/SimulatorServer.cpp
    src/SimulatorSession.cpp
)

# 头文件路径公开给可执行程序和测试
target_include_directories(devicescope_simulator_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 依赖 Qt Core/Network 和协议库
target_link_libraries(devicescope_simulator_core
    PUBLIC
        Qt6::Core
        Qt6::Network
        devicescope_proto
)

devicescope_set_warnings(devicescope_simulator_core)

# 可执行入口：只负责解析参数并启动服务器
add_executable(devicescope_simulator
    src/main.cpp
)

target_link_libraries(devicescope_simulator
    PRIVATE
        devicescope_simulator_core
)

# 应用项目统一的编译警告选项
devicescope_set_warnings(devicescope_simulator)

# 生成的可执行文件名：devicescope-simulator
set_target_properties(devicescope_simulator PROPERTIES
    OUTPUT_NAME devicescope-simulator
)