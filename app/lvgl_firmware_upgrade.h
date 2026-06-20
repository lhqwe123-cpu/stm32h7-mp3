/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file lvgl_firmware_upgrade.h
  * @brief 统一固件升级入口界面
 *
  * 提供统一的 Firmware Upgrade 界面，用户可选择:
  *   - SD 卡升级
  *   - 串口升级
  *   - 查看固件信息
  *   - 确认当前固件
 */

#ifndef __LVGL_FIRMWARE_UPGRADE_H__
#define __LVGL_FIRMWARE_UPGRADE_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
          * @brief 启动统一固件升级界面
          * @return 0 成功
     */
    int lvgl_firmware_upgrade_start(void);

    /**
          * @brief LVGL 事件回调包装 (用于 lv_obj_add_event_cb)
     */
    void lvgl_firmware_upgrade_start_cb(lv_event_t *e);

    /**
          * @brief 设置返回主菜单的回调函数
     */
    void lvgl_firmware_upgrade_set_back_to_menu_cb(lv_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __LVGL_FIRMWARE_UPGRADE_H__ */
