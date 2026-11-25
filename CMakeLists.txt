# ============================================================
# DeviceScope 顶层构建脚本
# 作用：定义全局编译选项、查找 Qt 依赖，并引入各子目录
# ============================================================
cmake_minimum_required(VERSION 3.21)

# 项目名、版本号和启用语言（C++）
project(DeviceScope VERSION 0.1.0 LANGUAGES CXX)

# 统一使用 C++20；关闭编译器扩展，保证 Linux/Windows 表现一致
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 开启 Qt 自动代码生成：moc（元对象）、uic（界面文件）、rcc（资源文件）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 统一输出目录：可执行文件到 <build>/bin，库到 <build>/lib
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# 加载 cmake/ 目录下的自定义模块（统一编译警告函数）
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# 查找 Qt 6.4+：Core 基础类型、Network TCP、Sql 历史数据、
# Test 单元测试、Widgets 桌面界面
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Sql Test Widgets)
qt_standard_project_setup()

# 启用 CTest，使 tests/ 中注册的测试可被 ctest 发现和执行
enable_testing()

# 按依赖顺序构建子模块：协议库 -> 模拟器 -> 客户端 -> 测试
add_subdirectory(proto)
add_subdirectory(simulator)
add_subdirectory(client)
add_subdirectory(tests)