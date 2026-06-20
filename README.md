# MP3 — STM32H743IIT6 LVGL 多媒体播放器

> **版本**: v0.1-beta  
> **状态**: 🚧 个人项目，暂不开源（仅作开发进程记录）  
> **许可证**: 暂未授权（All Rights Reserved）

基于 **Zephyr RTOS** + **LVGL v9.x** 的嵌入式多媒体播放器固件，运行于 STM32H743IIT6 平台，集成 MCUboot 安全启动与多种 OTA 固件升级方案。

---

## 📋 版本信息

| 项目 | 详情 |
|------|------|
| 固件版本 | v0.1-beta |
| 发布日期 | 2026-06 |
| Zephyr 版本 | 3.x (LTS) |
| LVGL 版本 | v9.x |
| MCUboot 版本 | 最新稳定版 |
| 目标平台 | STM32H743IIT6 (Cortex-M7, 480MHz) |
| 显示屏 | 7寸 RGB565 LCD (800×480) |
| 触摸屏 | GT1151Q (I2C 电容触摸) |

---

## ✅ 已实现功能

### 核心系统
- [x] Zephyr RTOS 多线程架构（LED 线程 + LVGL 显示线程）
- [x] 自定义堆内存管理 (`my_malloc`)
- [x] DTCM 栈分配优化（关键线程栈放置于 DTCM 区域）
- [x] Shell 控制台（UART1，支持文件系统命令）

### 显示与 UI
- [x] LTDC + SDRAM 驱动，RGB565 色彩格式
- [x] LVGL v9.x 图形库集成（Zephyr Module）
- [x] 双 VDB（Double VDB）无撕裂渲染
- [x] LVGL 性能监控（SYSMON + Perf Monitor）
- [x] GT1151Q 电容触摸驱动（I2C，多点触控）
- [x] 触摸轮询节流（20ms 间隔，保证滑动连续性）
- [x] 全屏刷新测试页面 (`lvgl_flush_test`)

### 存储与文件系统
- [x] SD 卡自动挂载（SDMMC1，4-bit 总线，FATFS）
- [x] 长文件名支持（LFN，最大 255 字符）
- [x] SD 卡图片解码与显示（JPEG 硬件解码，DMA2D + MDMA）
- [x] SD 卡视频文件扫描与列表

### 固件升级 (OTA)
- [x] **MCUboot 安全启动**（ECDSA P256 签名验证）
- [x] **SD 卡 OTA 升级** — 扫描固件包目录，解析 `.fwpkg` 固件包，写入 slot1
- [x] **串口 OTA 升级** — 基于 YModem-1K 协议，DMA 异步收发，CRC-16/CCITT 校验
- [x] 自定义固件包格式 (`.fwpkg`)：魔数 + 版本号 + SHA256 哈希
- [x] 统一固件升级入口界面 (`lvgl_firmware_upgrade`)
- [x] 固件包解析器 (`fwpkg_parser`)

### 外设驱动
- [x] STM32H7 JPEG 硬件编解码器驱动（MDMA 双缓冲乒乓传输）
- [x] GT1151Q 触摸控制器驱动
- [x] LCD LTDC 显示驱动
- [x] GPIO LED 控制
- [x] FMC SDRAM 驱动
- [x] SDMMC 驱动（SD 卡接口）
 

---

## 🚧 未来将会逐步实现的功能(x为已实现)

- [x] 视频播放功能（MJPEG / H.264 软解）
- [ ] 音频播放功能（I2S + 音频 DAC）
- [ ] SD 卡文件浏览器 UI
- [ ] 系统设置页面（亮度、音量、语言等）
- [x] 固件版本信息展示页面
- [ ] 开机动画 / Boot Logo
- [ ] 低功耗待机模式
- [ ] USB MSC（大容量存储）模式
- [ ] USB DFU 固件升级
- [ ] Wi-Fi / BLE 无线 OTA 升级
- [ ] 触摸校准功能
- [ ] 多语言支持
- [ ] 看门狗 (WDT) 集成
- [ ] 异常日志持久化存储

---

## 🗂️ 项目结构

```
mp3/
├── app/                          # LVGL 应用层模块
│   ├── lvgl_firmware_upgrade.c/h # 统一固件升级入口界面
│   ├── lvgl_flush_test.c/h       # 全屏刷新测试页面
│   ├── lvgl_sd_ota.c/h           # SD 卡 OTA 升级界面
│   ├── lvgl_sd_pic.c/h           # SD 卡图片显示
│   ├── lvgl_sd_video.c/h         # SD 卡视频列表
│   └── lvgl_uart_ota.c/h         # 串口 OTA 升级界面
├── bootloader/
│   ├── app/
│   │   └── mcuboot_upgrade.c/h   # MCUboot 升级应用层
│   └── lib/
│       ├── fwpkg_parser.h        # 固件包解析器
│       ├── uart_ota_protocol.c/h # 串口 OTA 协议定义
│       ├── uart_ota_server.c/h   # 串口 OTA 服务端（YModem）
│       └── ymodem.h              # YModem-1K 协议实现
├── boards/
│   └── stm32h743iit6.overlay     # 设备树覆盖文件
├── lib/                          # 硬件驱动层
│   ├── gt1151q.c/h               # GT1151Q 触摸驱动
│   ├── jpegcodec.c/h             # JPEG 硬件编解码器驱动
│   ├── lcd.c/h                   # LCD LTDC 显示驱动
│   └── my_malloc.c/h             # 自定义堆内存管理
├── mcuboot/                      # MCUboot 引导程序
│   ├── CMakeLists.txt
│   ├── prj.conf                  # MCUboot 配置
│   ├── keys/                     # 签名密钥
│   ├── build_mcuboot.sh          # 编译脚本
│   └── flash_mcuboot.ps1         # 烧录脚本
├── samples/
│   └── lvgl_demo/                # LVGL 示例项目（参考）
├── src/                          # 主程序
│   ├── main.c/h                  # 主入口 & SD 卡挂载
│   ├── led_thread.c/h            # LED 闪烁线程
│   └── video_thread.c/h          # LVGL 显示 & 触摸线程
├── CMakeLists.txt                # 顶层 CMake 构建文件
├── prj.conf                      # Zephyr 内核配置
├── flash_app.ps1                 # 应用烧录脚本
└── readme.md                     # 本文件
```

---

## 🔧 硬件平台

> ⚠️ **PCB 工程文件将一并上传至本仓库，详见 `hardware/` 目录（待添加）。**

| 组件 | 型号 / 规格 |
|------|------------|
| 主控 MCU | STM32H743IIT6 (Cortex-M7, 480MHz, 2MB Flash, 1MB RAM) |
| 外部 SDRAM | 待填写 |
| 外部 Flash | 待填写 |
| 显示屏 | 7寸 RGB565 LCD, 800×480 |
| 触摸控制器 | GT1151Q (I2C) |
| SD 卡槽 | SDMMC1, 4-bit 模式 |
| 调试串口 | USART1 (PA9/PA10) — Console & Shell |
| OTA 串口 | USART3 (PB10/PB11) — DMA 异步收发 |
| LED | PH4 (用户 LED) |

### PCB 信息

| 项目 | 详情 |
|------|------|
| PCB 版本 | 待填写 |
| 设计工具 | 待填写（如 KiCad / Altium Designer / LCEDA） |
| PCB 文件路径 | `hardware/`（待添加） |
| 原理图文件 | 待添加 |
| 备注 | 待填写 |

---

## 🛠️ 构建与烧录

### 前置条件

- [Zephyr RTOS 开发环境](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
- ARM GNU Toolchain (`arm-none-eabi-gcc`)
- Python 3.8+
- STM32CubeProgrammer 或 OpenOCD

### 编译 MCUboot Bootloader

```bash
cd mcuboot
./build_mcuboot.sh
```

### 编译应用固件

```bash
cd mp3
west build -b stm32h743iit6 -d build
```

### 烧录

```bash
# 烧录 MCUboot
cd mcuboot
./flash_mcuboot.ps1

# 烧录应用固件
cd mp3
./flash_app.ps1
```

---

## 📦 固件包格式 (`.fwpkg`，参照ymodem)

自定义固件包用于 OTA 升级，结构如下：

```
[Header: 64 bytes]
├── magic       : 4 bytes  — 魔数 "FWPK" (0x4B505746)
├── version     : 4 bytes  — 固件版本 (major.minor.revision.build)
├── image_size  : 4 bytes  — 固件镜像大小
├── image_hash  : 32 bytes — SHA256 哈希
└── reserved    : 20 bytes — 保留
[Firmware Image: variable]
```

---

## 📝 开发日志

| 日期 | 版本 | 变更说明 |
|------|------|---------|
| 2026-06 | v0.1-beta | 初始版本：LVGL 显示、触摸、SD卡、JPEG解码、MCUboot、SD/串口 OTA |

---

## ⚠️ 免责声明

本项目目前为个人开发记录，代码质量、稳定性和安全性尚未经过充分验证。在正式开源前，请勿用于生产环境。

---

> *README 最后更新: 2026-06-20*
