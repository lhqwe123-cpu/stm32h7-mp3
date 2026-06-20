/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file lvgl_uart_ota.c
 * @brief 串口 OTA 升级 LVGL 界面实现
 *
 * 提供串口 OTA 升级的用户界面，流程：
 * 1. 显示等待连接界面
 * 2. 后台启动 OTA 接收线程
 * 3. 实时显示接收进度
 * 4. 显示升级结果 (成功/失败)
 * 5. 成功后自动重启
 */

#include "lvgl_uart_ota.h"
#include "uart_ota_server.h"
#include "lvgl_sd_pic.h"
#include "mcuboot_upgrade.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>

#include "lvgl.h"
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(lvgl_uota, LOG_LEVEL_WRN);

/* ============================================================
 * 内部状态
 * ============================================================ */

static lv_obj_t *g_uota_scr = NULL;
static lv_obj_t *g_uota_status = NULL;
static lv_obj_t *g_uota_progress = NULL;
static lv_obj_t *g_uota_info_label = NULL;
static lv_obj_t *g_uota_back_btn = NULL;

static uota_server_t g_uota_server;
static bool g_uota_active = false;
static bool g_uota_complete = false;
static bool g_uota_failed = false;
static bool g_uota_ui_finalized = false; /* 防止重复创建终态UI */

/* OTA 接收线程 */
static struct k_thread g_uota_thread_data;
static K_THREAD_STACK_DEFINE(g_uota_thread_stack, 8192);

/* 跨线程共享状态 */
static volatile uota_state_t g_uota_state = UOTA_STATE_IDLE;
static volatile uint8_t g_uota_progress_val = 0;
static volatile bool g_uota_done = false;
static volatile int g_uota_error_code = 0;
static char g_uota_error_msg[128];

/* 返回主菜单回调 */
static lv_event_cb_t g_uota_back_to_menu_cb = NULL;

/* Back button handler - abort OTA and return */
static void on_uota_back_click(lv_event_t *e)
{
    /* Abort OTA transfer (async, don't block UI thread) */
    if (g_uota_active)
    {
        uota_server_abort(&g_uota_server);
        g_uota_active = false;
    }
    /* Delete current screen */
    if (g_uota_scr != NULL)
    {
        lv_obj_del(g_uota_scr);
        g_uota_scr = NULL;
    }
    /* Call back to menu callback */
    if (g_uota_back_to_menu_cb)
    {
        g_uota_back_to_menu_cb(NULL);
    }
}

/* 确认升级按钮回调 */
static void __attribute__((unused)) on_uota_confirm_click(lv_event_t *e)
{
    int ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
    if (ret == 0)
    {
        LOG_INF("MCUboot upgrade requested, rebooting...");
        printk("MCUboot upgrade requested, rebooting...\n");
        lv_label_set_text(g_uota_status, "Upgrade requested!\nRebooting...");
        lv_timer_handler();
        k_sleep(K_SECONDS(2));
        sys_reboot(SYS_REBOOT_COLD);
    }
    else
    {
        LOG_ERR("boot_request_upgrade failed: %d", ret);
        printk("boot_request_upgrade failed: %d\n", ret);
        lv_label_set_text(g_uota_status, "Confirm failed!\nPlease retry.");
    }
}

/* ============================================================
 * 回调函数
 * ============================================================ */

static void uota_state_callback(uota_state_t state, void *user_data)
{
    g_uota_state = state;
    LOG_INF("OTA state changed: %s", uota_state_str(state));
}

static void uota_progress_callback(uint8_t progress, void *user_data)
{
    g_uota_progress_val = progress;
}

static void uota_error_callback(int error_code, const char *msg,
                                void *user_data)
{
    g_uota_error_code = error_code;
    if (msg)
    {
        strncpy(g_uota_error_msg, msg, sizeof(g_uota_error_msg) - 1);
    }
    LOG_ERR("OTA error: %d - %s", error_code, msg ? msg : "unknown");
}

/* ============================================================
 * OTA 接收线程
 * ============================================================ */

static void uota_thread_entry(void *arg1, void *arg2, void *arg3)
{
    int ret;

    LOG_INF("UART OTA thread started");

    ret = uota_server_start(&g_uota_server);

    if (ret == 0)
    {
        LOG_INF("UART OTA completed successfully");
        g_uota_complete = true;
    }
    else if (g_uota_server.state == UOTA_STATE_ABORTED)
    {
        LOG_INF("UART OTA aborted by user");
        g_uota_failed = true;
    }
    else
    {
        LOG_ERR("UART OTA failed: %d", ret);
        g_uota_failed = true;
    }

    g_uota_done = true;
}

/* ============================================================
 * UI 刷新
 * ============================================================ */

static void uota_progress_ui_refresh(void)
{
    char buf[128];
    uota_state_t state = g_uota_state;
    uint8_t progress = g_uota_progress_val;

    /* 如果已经显示终态UI，不再重复刷新 */
    if (g_uota_ui_finalized)
    {
        return;
    }

    switch (state)
    {
    case UOTA_STATE_IDLE:
        lv_label_set_text(g_uota_status, "Initializing...");
        lv_bar_set_value(g_uota_progress, 0, LV_ANIM_OFF);
        break;

    case UOTA_STATE_WAIT_SYNC:
        lv_label_set_text(g_uota_status,
                          "Waiting for connection...\n"
                          "Connect UART and send firmware\n"
                          "USART3: PB10-TX, PB11-RX");
        lv_bar_set_value(g_uota_progress, 0, LV_ANIM_OFF);
        break;

    case UOTA_STATE_RECEIVING:
        snprintf(buf, sizeof(buf), "Receiving firmware... %u%%", progress);
        lv_label_set_text(g_uota_status, buf);
        lv_bar_set_value(g_uota_progress, progress, LV_ANIM_ON);
        break;

    case UOTA_STATE_VERIFYING:
        lv_label_set_text(g_uota_status, "Verifying firmware...");
        lv_bar_set_value(g_uota_progress, 98, LV_ANIM_ON);
        break;

    case UOTA_STATE_COMPLETE:
        g_uota_ui_finalized = true;
        lv_label_set_text(g_uota_status,
                          "Upgrade Complete!\n"
                          "Rebooting in 3 seconds...");
        lv_bar_set_value(g_uota_progress, 100, LV_ANIM_ON);
        LOG_INF("=== UART OTA complete, rebooting in 3s ===");
        printk("=== UART OTA complete, rebooting in 3s ===\n");
        lv_timer_handler();
        k_sleep(K_SECONDS(1));
        lv_label_set_text(g_uota_status,
                          "Upgrade Complete!\n"
                          "Rebooting in 2 seconds...");
        lv_timer_handler();
        k_sleep(K_SECONDS(1));
        lv_label_set_text(g_uota_status,
                          "Upgrade Complete!\n"
                          "Rebooting now...");
        lv_timer_handler();
        k_sleep(K_SECONDS(1));
        sys_reboot(SYS_REBOOT_COLD);
        break;

    case UOTA_STATE_FAILED:
    case UOTA_STATE_ABORTED:
        g_uota_ui_finalized = true;
        snprintf(buf, sizeof(buf), "Upgrade FAILED!\nErr: %d - %.80s",
                 g_uota_error_code,
                 g_uota_error_msg[0] ? g_uota_error_msg : "Unknown");
        lv_label_set_text(g_uota_status, buf);
        lv_bar_set_value(g_uota_progress, 0, LV_ANIM_OFF);

        /* 显示返回按钮 */
        {
            lv_obj_t *back_btn = lv_button_create(g_uota_scr);
            lv_obj_set_size(back_btn, 100, 40);
            lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
            lv_obj_add_event_cb(back_btn, on_uota_back_click, LV_EVENT_CLICKED, NULL);
            lv_obj_t *back_lbl = lv_label_create(back_btn);
            lv_label_set_text(back_lbl, "Back");
            lv_obj_center(back_lbl);
        }
        g_uota_active = false;
        break;

    default:
        break;
    }
}

/* ============================================================
 * 界面构建
 * ============================================================ */

static void show_uota_screen(void)
{
    lv_obj_clean(g_uota_scr);
    lv_obj_set_style_bg_color(g_uota_scr, lv_color_hex(0x101020), 0);
    lv_obj_set_style_pad_all(g_uota_scr, 0, 0);

    /* 背景图片 */
    const lv_image_dsc_t *bg_img = lvgl_sd_pic_get_image();
    if (bg_img != NULL && bg_img->data != NULL)
    {
        lv_obj_t *bg = lv_image_create(g_uota_scr);
        lv_image_set_src(bg, bg_img);
        lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_img_opa(bg, LV_OPA_30, 0);
        lv_obj_move_to_index(bg, 0);
    }

    /* 标题 */
    lv_obj_t *title = lv_label_create(g_uota_scr);
    lv_label_set_text(title, "UART OTA Upgrade");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 信息标签 */
    g_uota_info_label = lv_label_create(g_uota_scr);
    lv_label_set_text(g_uota_info_label,
                      "Connect UART and send .fwpkg file\n"
                      "Baudrate: 115200");
    lv_obj_set_style_text_color(g_uota_info_label, lv_color_white(), 0);
    lv_obj_align(g_uota_info_label, LV_ALIGN_TOP_MID, 0, 50);

    /* ״̬ */
    g_uota_status = lv_label_create(g_uota_scr);
    lv_label_set_text(g_uota_status, "Waiting for connection...");
    lv_obj_set_style_text_color(g_uota_status, lv_color_white(), 0);
    lv_obj_align(g_uota_status, LV_ALIGN_CENTER, 0, -20);

    /* 进度条 */
    g_uota_progress = lv_bar_create(g_uota_scr);
    lv_obj_set_size(g_uota_progress, 300, 20);
    lv_bar_set_range(g_uota_progress, 0, 100);
    lv_bar_set_value(g_uota_progress, 0, LV_ANIM_OFF);
    lv_obj_align(g_uota_progress, LV_ALIGN_CENTER, 0, 10);

    /* 返回按钮 */
    g_uota_back_btn = lv_button_create(g_uota_scr);
    lv_obj_set_size(g_uota_back_btn, 80, 35);
    lv_obj_align(g_uota_back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(g_uota_back_btn, on_uota_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(g_uota_back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_center(back_lbl);

    lv_timer_handler();
}

/* ============================================================
 * 公共接口
 * ============================================================ */

int lvgl_uart_ota_start(void)
{
    int ret;

    /* 安全检查：如果已经 active，先终止 */
    if (g_uota_active)
    {
        LOG_WRN("UART OTA already active, aborting previous");
        uota_server_abort(&g_uota_server);
        g_uota_active = false;
        g_uota_done = true;
        k_sleep(K_MSEC(300)); /* 等待旧线程退出 */
    }

    /* 删除旧 screen 释放内存 */
    if (g_uota_scr != NULL)
    {
        lv_obj_del(g_uota_scr);
        g_uota_scr = NULL;
    }

    g_uota_scr = lv_obj_create(NULL);
    g_uota_active = true;
    g_uota_complete = false;
    g_uota_failed = false;
    g_uota_done = false;
    g_uota_ui_finalized = false;
    g_uota_state = UOTA_STATE_IDLE;
    g_uota_progress_val = 0;
    g_uota_error_code = 0;
    memset(g_uota_error_msg, 0, sizeof(g_uota_error_msg));

    /* 初始化 OTA 服务器 */
    ret = uota_server_init(&g_uota_server, NULL, 115200);
    if (ret != 0)
    {
        LOG_ERR("uota_server_init failed: %d", ret);
        g_uota_active = false;
        return ret;
    }

    /* 设置回调 */
    uota_server_set_state_cb(&g_uota_server, uota_state_callback);
    uota_server_set_progress_cb(&g_uota_server, uota_progress_callback);
    uota_server_set_error_cb(&g_uota_server, uota_error_callback);

    /* 显示界面 */
    show_uota_screen();
    lv_screen_load(g_uota_scr);

    /* 启动 OTA 接收线程 (优先级低于 shell 的 7，确保不会饿死系统) */
    k_thread_create(&g_uota_thread_data,
                    g_uota_thread_stack,
                    K_THREAD_STACK_SIZEOF(g_uota_thread_stack),
                    uota_thread_entry,
                    NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    LOG_INF("UART OTA started");
    return 0;
}

/* LVGL 事件回调包装 */
void lvgl_uart_ota_start_cb(lv_event_t *e)
{
    lvgl_uart_ota_start();
}

void *lvgl_uart_ota_create_screen(void)
{
    lvgl_uart_ota_start();
    return g_uota_scr;
}

void lvgl_uart_ota_set_back_to_menu_cb(lv_event_cb_t cb)
{
    g_uota_back_to_menu_cb = cb;
}

void lvgl_uart_ota_progress_refresh(void)
{
    if (g_uota_active)
    {
        uota_progress_ui_refresh();
    }
}

bool lvgl_uart_ota_is_active(void)
{
    return g_uota_active;
}
