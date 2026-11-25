# 桌面客户端：基于 Qt Widgets 的图形界面程序
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

# 依赖 Qt Widgets 和协议库；Qt Network 将在“客户端连接骨架”步骤加入
target_link_libraries(devicescope_client
    PRIVATE
        Qt6::Widgets
        devicescope_proto
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