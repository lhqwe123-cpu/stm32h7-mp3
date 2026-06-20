# 串口 OTA 升级使用文档

## 概述

串口 OTA (UART OTA) 功能允许通过串口（UART）将固件升级包传输到设备端，实现固件远程升级。该功能基于 **YModem-1K 协议**，支持 115200 波特率及以上的高速传输，具备 CRC-16/CCITT 校验、自动重传、超时检测、断点续传等机制，确保数据传输的完整性和安全性。

### YModem 协议特性

- **握手协商**: 接收端发送 'C' 请求 CRC 模式传输
- **数据包大小**: 支持 128 字节 (SOH) 和 1024 字节 (STX) 两种模式
- **CRC 校验**: 使用 CRC-16/CCITT (多项式 0x1021)
- **自动重传**: NAK 触发发送端重传当前包
- **断点续传**: 支持 `--resume` 参数从指定偏移继续传输
- **DMA 传输**: 使用 STM32H743 DMA 进行 UART 异步收发

## 系统架构

```
┌─────────────────┐         UART (115200+)        ┌─────────────────┐
│   上位机/PC      │ ════════════════════════════ │   STM32H743     │
│  (发送固件包)    │       YModem 协议             │  (接收固件包)    │
│ ymodem_sender.py │                              │ uart_ota_server  │
└─────────────────┘                               └─────────────────┘
                                                          │
                                                          ▼
                                                  ┌─────────────────┐
                                                  │   Flash Slot1   │
                                                  │  (固件写入目标)  │
                                                  └─────────────────┘
                                                          │
                                                          ▼
                                                  ┌─────────────────┐
                                                  │    MCUboot      │
                                                  │  (引导升级验证)  │
                                                  └─────────────────┘
```

## 协议说明

### YModem 传输流程

```
接收端 (STM32H743)                    发送端 (PC)
      │                                    │
      │──── 'C' (握手请求) ──────────────?│
      │                                    │
      │?─── SOH 00 FF [文件名+大小] ──────│
      │                                    │
      │──── ACK + 'C' ──────────────────?│
      │                                    │
      │?─── STX 01 [1024字节数据] ────────│
      │──── ACK ────────────────────────?│
      │                                    │
      │         ... (重复直到完成) ...      │
      │                                    │
      │?─── EOT ──────────────────────────│
      │──── NAK ────────────────────────?│
      │?─── EOT ──────────────────────────│
      │──── ACK + 'C' ──────────────────?│
      │?─── SOH 00 FF [空文件名] ─────────│
      │──── ACK ────────────────────────?│
      │                                    │
      │        传输完成, 请求 MCUboot 升级   │
```

### 数据包格式

**SOH 包 (128 字节数据)**:
```
[SOH: 1B] [SEQ: 1B] [~SEQ: 1B] [DATA: 128B] [CRC_H: 1B] [CRC_L: 1B]
```

**STX 包 (1024 字节数据)**:
```
[STX: 1B] [SEQ: 1B] [~SEQ: 1B] [DATA: 1024B] [CRC_H: 1B] [CRC_L: 1B]
```

- **SOH** = 0x01 (128 字节包)
- **STX** = 0x02 (1024 字节包)
- **EOT** = 0x04 (传输结束)
- **ACK** = 0x06 (确认)
- **NAK** = 0x15 (否定确认)
- **CAN** = 0x18 (取消)
- **'C'** = 0x43 (CRC 模式请求)
- **SEQ**: 包序号 (1-255, 循环)
- **~SEQ**: 序号反码 (验证用)
- **CRC**: CRC-16/CCITT (高字节在前)

每个数据包固定格式如下：

```
┌────────┬────────┬────────┬────────┬────────┬──────────┬────────┬────────┐
│ SYNC1  │ SYNC2  │  SEQ   │  TYPE  │  LEN   │   DATA   │ CRC16  │  END   │
│ 0xA5   │ 0x5A   │ 2 bytes│ 1 byte │2 bytes │ N bytes  │2 bytes │0x0D 0x0A│
└────────┴────────┴────────┴────────┴────────┴──────────┴────────┴────────┘
```

- **SYNC**: 同步头 `0xA5 0x5A`
- **SEQ**: 包序号（大端，从 0 开始）
- **TYPE**: 包类型
- **LEN**: 数据长度（大端）
- **DATA**: 有效载荷
- **CRC16**: CRC-16/MODBUS 校验（覆盖 SEQ+TYPE+LEN+DATA）
- **END**: 包尾 `0x0D 0x0A`

### 包类型

| 类型 | 值 | 说明 |
|------|-----|------|
| CMD | 0x01 | 命令包 |
| DATA | 0x02 | 数据包 |
| ACK | 0x03 | 应答包（成功） |
| NACK | 0x04 | 否定应答包（失败） |
| EOT | 0x05 | 传输结束 |

### 命令码

| 命令 | 值 | 说明 |
|------|-----|------|
| CMD_START | 0x10 | 开始传输（携带固件头部信息） |
| CMD_DATA | 0x20 | 数据传输 |
| CMD_VERIFY | 0x30 | 请求校验 |
| CMD_ABORT | 0x40 | 中止传输 |
| CMD_COMPLETE | 0x50 | 传输完成 |

### 应答码

| 应答码 | 值 | 说明 |
|--------|-----|------|
| ACK_OK | 0x00 | 成功 |
| ACK_CRC_ERR | 0x01 | CRC 校验错误 |
| ACK_SEQ_ERR | 0x02 | 序号错误 |
| ACK_SIZE_ERR | 0x03 | 大小错误 |
| ACK_FLASH_ERR | 0x04 | Flash 写入错误 |
| ACK_BUSY | 0x05 | 设备忙 |
| ACK_ABORT | 0x06 | 中止 |

### 传输流程

```
发送端 (PC)                          接收端 (STM32H743)
    │                                       │
    │──── CMD_START (固件头部信息) ────────?│
    │                                       │ 验证固件信息
    │                                       │ 擦除 Slot1
    │?──── ACK_OK ────────────────────────│
    │                                       │
    │──── DATA seq=1 (1024 bytes) ────────?│
    │                                       │ 验证 CRC
    │                                       │ 写入 Flash
    │?──── ACK_OK seq=1 ─────────────────│
    │                                       │
    │──── DATA seq=2 (1024 bytes) ────────?│
    │?──── ACK_OK seq=2 ─────────────────│
    │                                       │
    │         ... (重复直到传输完成) ...      │
    │                                       │
    │──── CMD_VERIFY ─────────────────────?│
    │                                       │ 验证 Flash 数据完整性
    │?──── ACK_OK ────────────────────────│
    │                                       │
    │──── CMD_COMPLETE ───────────────────?│
    │                                       │ 请求 MCUboot 升级
    │?──── ACK_OK ────────────────────────│
    │                                       │
    │                                       │ 设备自动重启
```

## 使用方法

### 设备端操作

1. 将设备通过串口连接到 PC（使用 USART1: PA9-TX, PA10-RX）
2. 设备上电启动后，在 LVGL 主界面选择 **"UART Upgrade"** 按钮
3. 设备进入等待连接状态，LCD 显示 "Waiting for connection..."

### PC 端操作

PC 端使用项目自带的 `ymodem_sender.py` 发送工具，将固件文件通过 YModem 协议发送给设备。

#### 安装依赖

```bash
pip install pyserial
```

#### 基本用法

```bash
# Linux
python3 ymodem_sender.py /dev/ttyUSB0 firmware.fwpkg

# Windows
python ymodem_sender.py COM3 firmware.fwpkg

# 指定波特率
python3 ymodem_sender.py /dev/ttyUSB0 firmware.fwpkg 921600

# 使用 128 字节包 (兼容性更好)
python3 ymodem_sender.py /dev/ttyUSB0 firmware.fwpkg 115200 -s 128

# 使用 1024 字节包 (默认, 速度更快)
python3 ymodem_sender.py /dev/ttyUSB0 firmware.fwpkg 115200 -s 1024

# 断点续传 (从偏移 65536 字节继续)
python3 ymodem_sender.py /dev/ttyUSB0 firmware.fwpkg 115200 --resume 65536

# 列出可用串口
python3 ymodem_sender.py --list
```

#### 完整参数说明

```
usage: ymodem_sender.py [-h] [-s {128,256,1024}] [-r RESUME] [--list]
                        [port] [firmware] [baudrate]

参数:
  port              串口设备路径 (如 /dev/ttyUSB0, COM3)
  firmware          固件文件 (.fwpkg 或原始二进制)
  baudrate          波特率 (默认: 115200)
  -s, --packet-size 数据包大小: 128, 256, 1024 (默认: 1024)
  -r, --resume      断点续传偏移量 (字节)
  --list            列出可用串口并退出
```

#### 发送工具文件位置

发送工具位于: `bootloader/scrpt/ymodem_sender.py`

## 硬件连接

| STM32H743 | 串口工具 |
|-----------|---------|
| PA9 (USART1_TX) | RX |
| PA10 (USART1_RX) | TX |
| GND | GND |

## 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 波特率 | 115200 | 支持更高波特率（需硬件支持） |
| 数据位 | 8 | 固定 |
| 停止位 | 1 | 固定 |
| 校验位 | 无 | YModem 协议层使用 CRC-16/CCITT |
| 数据包大小 | 1024 字节 | 支持 128/256/1024 (ymodem.h 中配置) |
| 握手超时 | 60 秒 | 初始 'C' 握手超时 |
| 数据包超时 | 3 秒 | 单包接收超时 |
| 最大重试次数 | 10 | 握手和数据传输阶段 |

## 异常处理

### 传输中断 (断点续传)

如果传输过程中串口断开或数据中断：
- 设备端会在超时后自动进入失败状态
- **支持断点续传**: 重新发送时使用 `--resume <offset>` 参数
- 设备端从已接收的偏移量继续写入 Flash
- LCD 显示传输进度和状态

### CRC 校验失败

- 设备端检测到 CRC 错误后发送 NAK
- 发送端自动重传该数据包
- 超过最大重试次数后传输中止

### 序号错误

- 设备端检测到序号不匹配时发送 NAK
- 重复包（ACK丢失导致）会重新发送 ACK 而不重复写入

### Flash 写入失败

- 设备端检测到 Flash 写入错误时发送 CAN 取消传输
- 传输中止，需要重新开始

## 注意事项

1. **串口连接**: 确保串口连接稳定，建议使用短距离连接线
2. **电源稳定**: OTA 升级过程中不要断电，否则可能导致设备变砖
3. **固件格式**: 支持 `.fwpkg` 格式固件包或原始二进制文件
4. **版本兼容**: 确保固件包与硬件平台兼容
5. **MCUboot 配置**: 确保 MCUboot 已正确配置并烧录到设备
6. **Slot 分区**: 确保 Flash 分区表中 slot1 大小足够容纳新固件
7. **不要在升级中操作设备**: 升级过程中不要操作设备触摸屏或其他功能
8. **断点续传**: 传输中断后，记下已传输字节数，使用 `--resume` 参数继续

## 常见问题

### Q: 设备一直显示 "Waiting for connection..."
A: 检查串口连接是否正确（USART3: PB10-TX, PB11-RX），确认上位机已打开串口。

### Q: 传输过程中频繁出现 CRC 错误
A: 检查串口连接线质量，尝试降低波特率，确保没有电磁干扰。

### Q: 升级完成后设备无法启动
A: 可能是固件包损坏或不兼容。MCUboot 会自动回滚到旧固件。检查固件包是否正确生成。

### Q: 如何确认升级成功
A: 设备重启后，在 LVGL 界面选择 "SD Card Upgrade" -> "Firmware Info" 查看当前运行版本。

### Q: 如何使用断点续传？
A: 传输中断后，记下已传输的字节数（LCD 上会显示），然后使用:
   `python3 ymodem_sender.py /dev/ttyUSB0 firmware.fwpkg 115200 --resume <已传输字节数>`

## 文件结构

```
bootloader/lib/
├── ymodem.h               # YModem 协议定义
├── ymodem.c               # YModem 协议实现 (独立模块)
├── uart_ota_protocol.h    # 串口 OTA 协议定义 (兼容层)
├── uart_ota_protocol.c    # 串口 OTA 协议实现 (兼容层)
├── uart_ota_server.h      # 串口 OTA 服务端接口
└── uart_ota_server.c      # 串口 OTA 服务端实现 (基于 YModem)

bootloader/scrpt/
├── ymodem_sender.py       # YModem 固件发送工具 (推荐)
└── uart_ota_sender.py     # 旧版自定义协议发送工具 (已弃用)

app/
├── lvgl_uart_ota.h        # 串口 OTA LVGL 界面接口
└── lvgl_uart_ota.c        # 串口 OTA LVGL 界面实现
```
