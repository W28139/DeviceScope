# DeviceScope

基于 C++20 和 Qt 6 的跨平台设备监控与调试工作站。

项目由两个可执行程序组成：

- **DeviceScope Client**：Qt Widgets 桌面客户端，负责连接、遥测展示、控制命令、报警、历史数据和日志；
- **Device Simulator**：设备模拟器，负责生成遥测、响应命令、产生报警和注入故障。

当前进度：**步骤 1（协议常量与编解码）**。协议库已提供 22 字节报文头、CRC32 和 7 类消息的编解码；客户端仍显示占位窗口，模拟器仍为 TCP 回显服务。

## 文档

- [开发计划](docs/DeviceScope-开发计划.md)：定位、范围、架构、协议、测试与最终验收标准；
- [开发步骤](docs/DeviceScope-开发步骤.md)：按依赖顺序排列的 17 个开发步骤及每步验收标准；
- [协议定义](docs/protocol.md)：报文格式、CRC32、消息类型与负载布局。

## 环境要求

- CMake 3.21 或更高；
- 支持 C++20 的编译器：GCC 11+、Clang 14+ 或 MSVC 19.29+；
- Qt 6.4 或更高，至少包含 Core、Widgets、Network、Sql、Test 模块；
- Ninja（推荐）；
- Linux 桌面环境或 WSL2 + WSLg；Windows 使用 Qt MSVC/MinGW 套件。

Ubuntu / WSL2 (Ubuntu 24.04) 安装命令：

```bash
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite
```

## 构建

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug -j
```

构建产物位于 `build/linux-debug/bin/`。

Release 构建：

```bash
cmake --preset linux-release
cmake --build --preset linux-release -j
```

## 运行

启动模拟器（默认监听 `127.0.0.1:9000`）：

```bash
./build/linux-debug/bin/devicescope-simulator --port 9000
```

启动客户端：

```bash
./build/linux-debug/bin/devicescope-client
```

当前模拟器是回显服务，可以用 `nc` 验证：

```bash
nc 127.0.0.1 9000
# 输入任意文本，模拟器会原样返回
```

## 测试

```bash
ctest --preset linux-debug --output-on-failure
```

## 目录结构

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/                  # 编译选项等 CMake 模块
├── proto/                  # 协议库：报文头、CRC32、消息编解码（解包在步骤 2）
│   ├── include/devicescope/protocol/
│   └── src/
├── simulator/              # 设备模拟器
│   └── src/
├── client/                 # Qt 客户端
│   └── src/
│       ├── app/
│       └── ui/
└── tests/                  # 单元测试与集成测试
```

## Windows 构建说明

步骤 15 会完成 Windows 构建与打包的完整验证。当前阶段只要求：

1. 安装 Qt 6.4+（MSVC 2019/2022 或 MinGW）和 CMake；
2. 使用 Qt 提供的 CMake 或设置 `CMAKE_PREFIX_PATH` 指向 Qt 安装目录；
3. 执行与 Linux 相同的 CMake 配置和构建命令。

## 许可

项目代码使用 MIT License。Qt 依赖遵循其 LGPLv3 许可要求。