# MCUboot 固件升级使用文档

## 1. 概述

本项目基于 Zephyr RTOS 和 MCUboot bootloader 实现固件升级功能。MCUboot 是一个安全的 bootloader，支持固件签名验证、双槽位交换升级、回滚等功能。

### 1.1 硬件平台

- **MCU**: STM32H743IIT6
- **Flash**: 2MB 内置 Flash
- **存储**: SD 卡 (用于存放固件包)

### 1.2 Flash 分区布局

| 分区名称 | 起始地址 | 大小 | 用途 |
|---------|---------|------|------|
| boot_partition | 0x08000000 | 128KB | MCUboot bootloader |
| storage_partition | 0x08020000 | 128KB | 设置存储 |
| slot0_partition | 0x08040000 | 768KB | 主镜像 (当前固件) |
| slot1_partition | 0x08100000 | 768KB | 升级镜像 (新固件) |
| scratch_partition | 0x081C0000 | 128KB | Swap 暂存区 |

## 2. 工程结构

```
zephyr_workplace/
├── mcuboot/                          # MCUboot bootloader 工程
│   ├── CMakeLists.txt                # 构建配置
│   ├── prj.conf                      # Kconfig 配置
│   ├── boards/stm32h743iit6.overlay  # 设备树 overlay
│   ├── keys/                         # 签名密钥目录
│   ├── generate_keys.sh              # 密钥生成脚本
│   └── build_mcuboot.sh              # 编译脚本
│
└── lvgl_demo/                        # 应用工程
    ├── prj.conf                      # 应用配置 (含 MCUboot 支持)
    ├── boards/stm32h743iit6.overlay  # 设备树 overlay (含分区)
    ├── CMakeLists.txt                # 构建配置
    └── bootloader/                   # MCUboot 相关代码
        ├── app/                      # 应用层
        │   ├── mcuboot_upgrade.h     # 升级接口头文件
        │   ├── mcuboot_upgrade.c     # 升级接口实现
        │   ├── mcuboot_test.h        # 测试模块头文件
        │   └── mcuboot_test.c        # 测试模块实现
        └── lib/                      # 辅助库
            ├── fwpkg_parser.h        # 固件包解析器
            ├── fwpkg_parser.c
            ├── upgrade_monitor.h     # 升级状态监控器
            ├── upgrade_monitor.c
            ├── flash_helper.h        # Flash 操作辅助
            └── flash_helper.c
```

## 3. 配置参数说明

### 3.1 MCUboot Bootloader 配置 (`mcuboot/prj.conf`)

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| CONFIG_MAIN_STACK_SIZE | 主栈大小 | 10240 |
| CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256 | ECDSA P256 签名 | y |
| CONFIG_BOOT_IMG_HASH_ALG_SHA256 | SHA256 哈希 | y |
| CONFIG_MCUBOOT_LOG_LEVEL_INF | 日志级别 INFO | y |
| CONFIG_BOOT_BANNER | 显示启动信息 | y |

### 3.2 应用配置 (`lvgl_demo/prj.conf`)

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| CONFIG_BOOTLOADER_MCUBOOT | 启用 MCUboot 支持 | y |
| CONFIG_IMG_MANAGER | 启用镜像管理 | y |
| CONFIG_MCUBOOT_BOOTUTIL_LIB | 启用 bootutil 库 | y |
| CONFIG_MPU_ALLOW_FLASH_WRITE | 允许 Flash 写入 | y |
| CONFIG_USE_DT_CODE_PARTITION | 使用代码分区 | y |
| CONFIG_FLASH_MAP | 启用 Flash Map | y |
| CONFIG_STREAM_FLASH | 启用 Stream Flash | y |
| CONFIG_HEAP_MEM_POOL_SIZE | 堆内存大小 | 16384 |

## 4. 升级流程

### 4.1 整体流程

```
┌──────────────────────────────────────────────────────────────┐
│                     固件升级流程                              │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  1. 将 .fwpkg 固件包放入 SD 卡 /FIRMWARE 目录                │
│                          ↓                                   │
│  2. 应用扫描 SD 卡, 找到固件包                                │
│                          ↓                                   │
│  3. 解析固件包头部, 验证魔数和版本                            │
│                          ↓                                   │
│  4. 擦除 slot1_partition (升级镜像分区)                       │
│                          ↓                                   │
│  5. 将固件镜像写入 slot1_partition                            │
│                          ↓                                   │
│  6. 验证写入数据完整性                                        │
│                          ↓                                   │
│  7. 调用 boot_request_upgrade() 请求 MCUboot 执行升级         │
│                          ↓                                   │
│  8. 重启设备                                                  │
│                          ↓                                   │
│  9. MCUboot 验证 slot1 镜像签名, 交换 slot0/slot1             │
│                          ↓                                   │
│ 10. 启动新固件                                                │
│                          ↓                                   │
│ 11. 新固件调用 boot_write_img_confirmed() 确认升级             │
│                          ↓                                   │
│ 12. 升级完成                                                  │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 升级模式

MCUboot 支持两种升级模式：

- **TEST 模式** (`BOOT_UPGRADE_TEST`): 新固件运行一次，如果未确认则回滚
- **PERMANENT 模式** (`BOOT_UPGRADE_PERMANENT`): 新固件永久替换旧固件

本项目默认使用 **TEST 模式**，新固件需要在运行后调用 `boot_write_img_confirmed()` 确认。

### 4.3 回滚机制

如果新固件在 TEST 模式下运行后崩溃或未确认，MCUboot 会在下次启动时自动回滚到旧固件。

## 5. API 参考

### 5.1 应用层 API (`mcuboot_upgrade.h`)

#### 固件包扫描
```c
int mcuboot_upgrade_scan_package(char *out_path, size_t path_size);
```
扫描 SD 卡 `/SD:/FIRMWARE` 目录下的 `.fwpkg` 文件。

#### 读取固件包头部
```c
int mcuboot_upgrade_read_header(const char *path, fwpkg_header_t *header);
```
解析固件包头部，获取版本、大小等信息。

#### 执行升级
```c
int mcuboot_upgrade_perform(const char *path,
                            const fwpkg_header_t *header,
                            upgrade_progress_cb_t cb,
                            void *user_data);
```
执行完整升级流程：擦除 → 写入 → 验证 → 请求升级。

#### 确认升级
```c
int mcuboot_upgrade_confirm(void);
```
将当前固件标记为永久有效，防止回滚。

#### 获取版本
```c
int mcuboot_upgrade_get_running_version(uint32_t *version);
int mcuboot_upgrade_get_slot1_version(uint32_t *version);
```

#### 启动升级 UI
```c
int mcuboot_upgrade_ui_start(void);
```
启动 LVGL 固件升级界面。

### 5.2 辅助库 API

#### 固件包解析器 (`fwpkg_parser.h`)
```c
fwpkg_result_t fwpkg_scan(const char *dir_path, char *out_path, size_t path_size);
fwpkg_result_t fwpkg_parse_header(const char *file_path, fwpkg_info_t *info);
fwpkg_result_t fwpkg_verify(const char *file_path);
fwpkg_result_t fwpkg_read_image_data(const char *file_path, uint8_t *buf,
                                     uint32_t offset, uint32_t len, uint32_t *out_read);
```

#### 升级状态监控器 (`upgrade_monitor.h`)
```c
int um_init(um_monitor_t *monitor, um_stage_cb_t stage_cb,
            um_progress_cb_t progress_cb, void *user_data);
int um_send_event(um_monitor_t *monitor, um_event_t event);
int um_update_write_progress(um_monitor_t *monitor, uint32_t bytes_written);
bool um_is_timeout(um_monitor_t *monitor);
```

#### Flash 操作辅助 (`flash_helper.h`)
```c
flash_result_t flash_get_part_info(flash_area_id_t area_id, flash_part_info_t *info);
flash_result_t flash_erase_area(flash_area_id_t area_id);
flash_result_t flash_write_area(flash_area_id_t area_id, uint32_t offset,
                                const uint8_t *data, uint32_t len);
flash_result_t flash_read_area(flash_area_id_t area_id, uint32_t offset,
                               uint8_t *data, uint32_t len);
flash_result_t flash_verify_area(flash_area_id_t area_id, uint32_t offset,
                                 const uint8_t *data, uint32_t len);
```

## 6. 固件包格式

### 6.1 .fwpkg 文件结构

```
┌──────────────────────────────────────┐
│  Header (64 bytes)                   │
│  ┌──────────────────────────────────┐│
│  │ magic       : 4 bytes  "FWPK"    ││
│  │ version     : 4 bytes  版本号    ││
│  │ image_size  : 4 bytes  镜像大小  ││
│  │ image_hash  : 32 bytes SHA256    ││
│  │ reserved    : 20 bytes 保留      ││
│  └──────────────────────────────────┘│
├──────────────────────────────────────┤
│  Firmware Image (variable)           │
│  ┌──────────────────────────────────┐│
│  │ 原始 MCUboot 格式的固件镜像      ││
│  │ (包含 MCUboot 头部 + 固件数据)   ││
│  └──────────────────────────────────┘│
└──────────────────────────────────────┘
```

### 6.2 版本号格式

版本号使用 32 位整数编码：
- `bits 31:24` - major (主版本)
- `bits 23:16` - minor (次版本)
- `bits 15:8`  - revision (修订号)
- `bits 7:0`   - build_num (构建号)

## 7. 编译与烧录

### 7.1 首次部署

```bash
# 1. 生成签名密钥
cd mcuboot
./generate_keys.sh

# 2. 编译 MCUboot bootloader
./build_mcuboot.sh

# 3. 烧录 MCUboot
flash write build/zephyr/zephyr.bin 0x08000000

# 4. 编译应用 (带 MCUboot 签名)
cd ../lvgl_demo
west build -b stm32h743iit6

# 5. 使用 imgtool 签名应用固件
imgtool sign --key ../mcuboot/keys/root-ec-p256.pem \
    --header-size 0x400 \
    --align 8 \
    --version 1.0.0 \
    --slot-size 0xC0000 \
    build/zephyr/zephyr.bin \
    build/zephyr/zephyr.signed.bin

# 6. 烧录签名后的应用
flash write build/zephyr/zephyr.signed.bin 0x08040000
```

### 7.2 OTA 升级

```bash
# 1. 编译新版本应用
west build -b stm32h743iit6

# 2. 签名
imgtool sign --key ../mcuboot/keys/root-ec-p256.pem \
    --header-size 0x400 \
    --align 8 \
    --version 1.0.1 \
    --slot-size 0xC0000 \
    build/zephyr/zephyr.bin \
    build/zephyr/zephyr.signed.bin

# 3. 打包为 .fwpkg
python3 bootloader/fwpkg_pack.py pack \
    build/zephyr/zephyr.signed.bin \
    1.0.1 \
    firmware_v1.0.1.fwpkg

# 4. 将 .fwpkg 复制到 SD 卡 /FIRMWARE 目录

# 5. 在设备上通过 LVGL 界面或命令行触发升级
```

### 7.3 fwpkg_pack.py 工具用法

```bash
# 打包固件
python3 fwpkg_pack.py pack <signed_image.bin> <version> [output.fwpkg]

# 查看固件包信息
python3 fwpkg_pack.py info <input.fwpkg>

# 解包固件
python3 fwpkg_pack.py unpack <input.fwpkg> <output_image.bin>

# 示例
python3 fwpkg_pack.py pack zephyr.signed.bin 1.0.1 firmware_v1.0.1.fwpkg
python3 fwpkg_pack.py info firmware_v1.0.1.fwpkg
```

## 8. 测试

### 8.1 运行测试

```c
// 在应用代码中调用
#include "mcuboot_test.h"

void run_tests(void)
{
    test_suite_result_t result;
    mcuboot_test_run_all(&result);
    mcuboot_test_print_results(&result);
}
```

### 8.2 生成测试固件包

```c
char fwpkg_path[256];
mcuboot_test_generate_fwpkg("/SD:/FIRMWARE",
                            0x01000001,  // v1.0.1
                            1024 * 100,  // 100KB
                            fwpkg_path,
                            sizeof(fwpkg_path));
```

## 9. 注意事项

1. **分区一致性**: MCUboot 和应用工程的 flash 分区定义必须完全一致
2. **签名密钥**: 私钥必须妥善保管，泄露会导致安全风险
3. **版本号**: 新固件的版本号必须大于当前版本
4. **镜像大小**: 固件镜像不能超过 slot 分区大小 (768KB)
5. **确认升级**: TEST 模式下必须调用 `boot_write_img_confirmed()` 确认
6. **Flash 寿命**: 频繁升级会消耗 Flash 写入寿命
7. **断电保护**: MCUboot 的 swap 模式支持断电恢复
