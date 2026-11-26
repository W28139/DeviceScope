# 客户端核心库：网络层，供 UI 和测试共用
add_library(devicescope_client_core STATIC
    src/network/ConnectionWorker.cpp
    src/network/ConnectionWorker.h
    src/network/NetworkClient.cpp
    src/network/NetworkClient.h
    src/network/ConnectionState.h
)

target_include_directories(devicescope_client_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(devicescope_client_core
    PUBLIC
        Qt6::Core
        Qt6::Network
        devicescope_proto
)

devicescope_set_warnings(devicescope_client_core)

# 桌面客户端：现在只有连接骨架，业务页在步骤 7–12 填充
add_executable(devicescope_client
    src/app/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
)

# 头文件搜索路径：支持 #include "ui/MainWindow.h"
target_include_directories(devicescope_client
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 依赖 Qt Widgets 和客户端网络库
target_link_libraries(devicescope_client
    PRIVATE
        Qt6::Widgets
        devicescope_client_core
)

if(WIN32)
    # Windows 下作为 GUI 程序启动，双击运行时不额外弹出控制台窗口
    set_target_properties(devicescope_client PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# 应用项目统一的编译警告选项
devicescope_set_warnings(devicescope_client)

# 生成的可执行文件名：devicescope-client
set_target_properties(devicescope_client PROPERTIES
    OUTPUT_NAME devicescope-client
)