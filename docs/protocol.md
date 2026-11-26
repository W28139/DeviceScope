# DeviceScope 通信协议

> 版本：v1（对应开发步骤 1）
> 传输层：TCP 字节流
> 字节序：所有多字节字段使用网络字节序（大端）
> 时间戳：UTC 毫秒，`u64`
> 字符串：`u16` 字节长度 + UTF-8 字节，最长 65535 字节
> 单包上限：负载 1 MiB

本文档固化开发计划第 7 章的协议设计，是客户端与模拟器共同遵守的接口约定。修改本文档即代表修改协议，需要同步递增 `Version` 并更新双方代码与测试。

## 1. 报文格式

每个报文由 **22 字节固定报文头 + 变长负载**组成。

| 偏移 | 字段 | 长度 | 说明 |
|---:|---|---:|---|
| 0 | Magic | 2 | 固定 `0x4453`（ASCII `"DS"`） |
| 2 | Version | 1 | 当前为 `1`，握手阶段协商 |
| 3 | Message Type | 1 | 见第 3 节 |
| 4 | Flags | 2 | bit0 = 响应标记，其余保留 |
| 6 | Request ID | 4 | 客户端产生，响应原样带回 |
| 10 | Payload Length | 4 | 负载字节数，不得超过 1 MiB |
| 14 | Header CRC | 4 | CRC32(偏移 0–13 的 14 字节) |
| 18 | Payload CRC | 4 | CRC32(完整负载) |
| 22 | Payload | N | 消息体，见第 6 节 |

空负载时 `Payload CRC = 0x00000000`。

## 2. CRC32

- 多项式：`0xEDB88320`（反射形式）
- 初值：`0xFFFFFFFF`
- 结果异或：`0xFFFFFFFF`
- 参考向量：`"123456789"` → `0xCBF43926`
- **Header CRC** 覆盖报文头前 14 字节（Magic 到 Payload Length）；
- **Payload CRC** 覆盖完整负载；两个 CRC 分别计算、分别校验。

## 3. 消息类型

| 值 | 类型 | 方向 | 用途 |
|---:|---|---|---|
| 0x01 | HELLO | 双向 | 建立会话与版本协商 |
| 0x02 | HEARTBEAT | 双向 | 连接保活 |
| 0x03 | TELEMETRY | 设备 → 客户端 | 温度、压力、转速等遥测 |
| 0x04 | COMMAND | 客户端 → 设备 | 启动、停止、复位 |
| 0x05 | COMMAND_ACK | 设备 → 客户端 | 命令执行结果 |
| 0x06 | ALARM | 设备 → 客户端 | 报警事件 |
| 0x07 | ERROR | 双向 | 协议或设备错误 |

## 4. 枚举

### 4.1 DeviceState（设备状态）

| 值 | 名称 | 说明 |
|---:|---|---|
| 0 | Idle | 空闲 |
| 1 | Running | 运行中 |
| 2 | Stopped | 已停止 |
| 3 | Fault | 故障 |

### 4.2 CommandId（命令编号）

| 值 | 名称 |
|---:|---|
| 1 | Start |
| 2 | Stop |
| 3 | Reset |

### 4.3 AckResult（命令/握手结果）

| 值 | 名称 | 说明 |
|---:|---|---|
| 0 | Ok | 成功 |
| 1 | Rejected | 被拒绝 |
| 2 | Failed | 执行失败 |

### 4.4 AlarmSeverity（报警级别）

| 值 | 名称 |
|---:|---|
| 0 | Info |
| 1 | Warning |
| 2 | Critical |

### 4.5 AlarmState（报警状态）

| 值 | 名称 | 说明 |
|---:|---|---|
| 0 | Raised | 报警产生 |
| 1 | Cleared | 报警清除 |

## 5. Flags

| 位 | 名称 | 说明 |
|---:|---|---|
| bit0 | Response | 1 表示该报文是对某个请求的响应 |
| bit1–15 | 保留 | 发送方填 0；接收方当前不因非 0 值报错 |

## 6. 负载布局

字段按表中顺序紧密排列；`string` 为 `u16 字节长度 + UTF-8 字节`；`f32` 为 IEEE-754 单精度浮点数（大端位模式）。

### 6.1 HELLO 请求（客户端 → 设备）

| 字段 | 类型 | 说明 |
|---|---|---|
| clientName | string | 客户端名称 |

### 6.2 HELLO 响应（设备 → 客户端）

| 字段 | 类型 | 说明 |
|---|---|---|
| deviceId | u16 | 设备编号 |
| deviceName | string | 设备名称 |
| capabilities | u32 | 能力位掩码（预留） |
| result | u8 (AckResult) | 握手结果 |

### 6.3 HEARTBEAT

| 字段 | 类型 | 说明 |
|---|---|---|
| timestampMs | u64 | UTC 毫秒时间戳 |

### 6.4 TELEMETRY

| 字段 | 类型 | 说明 |
|---|---|---|
| timestampMs | u64 | UTC 毫秒时间戳 |
| temperatureC | f32 | 温度（摄氏度） |
| pressureKPa | f32 | 压力（kPa） |
| rpm | f32 | 转速（rpm） |
| deviceState | u8 (DeviceState) | 设备状态 |

### 6.5 COMMAND

| 字段 | 类型 | 说明 |
|---|---|---|
| commandId | u8 (CommandId) | 命令编号 |
| parameter | u32 | 第一版填 0，预留 |

### 6.6 COMMAND_ACK

| 字段 | 类型 | 说明 |
|---|---|---|
| commandId | u8 (CommandId) | 对应的命令编号 |
| result | u8 (AckResult) | 执行结果 |
| message | string | 结果描述，可为空 |

### 6.7 ALARM

| 字段 | 类型 | 说明 |
|---|---|---|
| alarmId | u32 | 报警实例编号 |
| code | u16 | 报警代码 |
| severity | u8 (AlarmSeverity) | 报警级别 |
| timestampMs | u64 | 报警时间（UTC 毫秒） |
| state | u8 (AlarmState) | Raised / Cleared |
| message | string | 报警描述，可为空 |

### 6.8 ERROR

| 字段 | 类型 | 说明 |
|---|---|---|
| code | u16 | 错误代码 |
| message | string | 错误描述，可为空 |

## 7. 版本协商

1. 客户端发送 HELLO 请求，报文头 `Version` 填自己支持的协议版本；
2. 设备校验 `Version`：不一致时回一条 ERROR 并断开连接（步骤 3 实现）；
3. 一致时设备回 HELLO 响应，随后开始发送遥测。

## 8. 编解码规则

协议库所有入口返回 `Result<T>` 或 `ErrorCode`，**不跨模块抛异常**。

`decodeHeader(frame)` 依次检查：

1. 长度不足 22 字节 → `TruncatedHeader`；
2. Magic 不匹配 → `InvalidMagic`；
3. Version 不等于当前版本 → `UnsupportedVersion`；
4. Payload Length 超过 1 MiB → `PayloadTooLarge`（致命错误，调用方应断开连接）；
5. Header CRC 不匹配 → `HeaderCrcMismatch`。

`verifyPayload(header, payload)` 检查：

1. 实际负载小于声明长度 → `TruncatedPayload`；
2. 实际负载大于声明长度 → `PayloadLengthMismatch`；
3. CRC 不匹配 → `PayloadCrcMismatch`。

消息负载解码规则：

- 字段必须恰好消费完负载；截断或存在多余字节 → `MalformedPayload`；
- 字符串长度不得超过剩余字节，且内容必须是合法 UTF-8，否则 → `MalformedPayload`；
- 枚举字段取值越界 → 对应的 `InvalidDeviceState` / `InvalidCommandId` / `InvalidAckResult` / `InvalidAlarmSeverity` / `InvalidAlarmState`；
- 长度字段超过 1 MiB 的报文绝不能按该长度分配内存；
- 未知消息类型不得导致崩溃（步骤 2 的流式解包会保留原始负载）。

## 9. 错误码

| 错误码 | 含义 |
|---|---|
| Ok | 成功 |
| InvalidMagic | 魔数不匹配 |
| UnsupportedVersion | 协议版本不支持 |
| TruncatedHeader | 报文头不足 22 字节 |
| HeaderCrcMismatch | Header CRC 校验失败 |
| PayloadTooLarge | 长度字段超过 1 MiB |
| TruncatedPayload | 负载小于声明长度 |
| PayloadLengthMismatch | 负载大于声明长度 |
| PayloadCrcMismatch | Payload CRC 校验失败 |
| MalformedPayload | 负载字段截断、多余字节或非法 UTF-8 |
| StringTooLong | 字符串超过 65535 字节 |
| InvalidDeviceState | DeviceState 取值越界 |
| InvalidCommandId | CommandId 取值越界 |
| InvalidAckResult | AckResult 取值越界 |
| InvalidAlarmSeverity | AlarmSeverity 取值越界 |
| InvalidAlarmState | AlarmState 取值越界 |

## 10. 后续步骤

- 步骤 2：流式解包（`StreamDecoder`）、半包/粘包处理、坏包重同步与统计；
- 步骤 2：手写十六进制黄金向量（不得由被测代码生成）；
- 步骤 6：心跳超时、请求匹配与自动重连规则。