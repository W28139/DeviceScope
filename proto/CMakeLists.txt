# 协议静态库：客户端与模拟器共享的协议常量、编码与解码实现
add_library(devicescope_proto STATIC
    src/Version.cpp   # 协议版本描述
    src/Crc32.cpp     # CRC32 表驱动实现
    src/Codec.cpp     # 报文头编解码与负载校验
    src/Message.cpp   # 各消息体的编解码
)

# 以 PUBLIC 暴露 include/，链接方可直接 #include "devicescope/protocol/..."
target_include_directories(devicescope_proto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# 依赖 Qt Core（QString 等基础类型）；PUBLIC 表示该依赖会传递给链接方
target_link_libraries(devicescope_proto
    PUBLIC
        Qt6::Core
)

# 应用项目统一的编译警告选项
devicescope_set_warnings(devicescope_proto)