/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file lvgl_uart_ota.h
 * @brief 串口 OTA 升级 LVGL 界面
 *
 * 提供串口 OTA 升级的用户界面，包括：
 * - 等待连接界面
 * - 接收进度显示
 * - 升级结果提示
 */

#ifndef __LVGL_UART_OTA_H__
#define __LVGL_UART_OTA_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 启动串口 OTA 升级界面
     *
     * 显示等待连接的界面，并在后台启动 OTA 接收线程。
     *
     * @return 0 成功, 负值失败
     */
    int lvgl_uart_ota_start(void);

    /**
     * @brief LVGL 事件回调包装 (用于 lv_obj_add_event_cb)
     */
    void lvgl_uart_ota_start_cb(lv_event_t *e);

    /**
     * @brief 创建串口 OTA 升级界面 screen
     *
     * @return screen 对象
     */
    void *lvgl_uart_ota_create_screen(void);

    /**
     * @brief 设置返回主菜单的回调函数
     *
     * @param cb 回调函数
     */
    void lvgl_uart_ota_set_back_to_menu_cb(lv_event_cb_t cb);

    /**
     * @brief 刷新串口 OTA 进度 UI (由 LVGL 主循环调用)
     */
    void lvgl_uart_ota_progress_refresh(void);

    /**
     * @brief 获取串口 OTA 是否正在进行中
     *
     * @return true 进行中, false 空闲
     */
    bool lvgl_uart_ota_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* __LVGL_UART_OTA_H__ */
