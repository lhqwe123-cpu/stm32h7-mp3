/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file lvgl_firmware_upgrade.c
 * @brief 统一固件升级入口界面实现
 *
 * 提供统一的 Firmware Upgrade 界面:
 *   - SD Card Upgrade  -> 进入 SD 卡固件包列表
 *   - UART Upgrade      -> 进入串口 OTA 等待界面
 *   - Firmware Info     -> 弹窗显示版本信息
 *   - Confirm FW        -> 确认当前固件为永久
 *   - Back              -> 返回主菜单
 */

#include "lvgl_firmware_upgrade.h"
#include "lvgl_sd_ota.h"
#include "lvgl_uart_ota.h"
#include "lvgl_sd_pic.h"
#include "mcuboot_upgrade.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "lvgl.h"
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(lvgl_fw_upg, LOG_LEVEL_INF);

/* ============================================================
 * 内部状态
 * ============================================================ */

static lv_obj_t *g_fw_upg_scr = NULL;
static lv_event_cb_t g_back_to_menu_cb = NULL;

/* ============================================================
 * Firmware Info 弹窗
 * ============================================================ */

static void on_info_close_click(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_user_data(e);
    if (mbox)
    {
        lv_obj_del(mbox);
    }
}

static void show_firmware_info(lv_event_t *e)
{
    uint32_t cur_ver = 0;
    char buf[256];
    int off = 0;

    if (mcuboot_upgrade_get_running_version(&cur_ver) == 0)
    {
        off += snprintf(buf + off, sizeof(buf) - off,
                        "Running: %u.%u.%u\n",
                        (cur_ver >> 24) & 0xFF, (cur_ver >> 16) & 0xFF,
                        (cur_ver >> 8) & 0xFF);
    }
    else
    {
        off += snprintf(buf + off, sizeof(buf) - off, "Running: unknown\n");
    }

    uint32_t slot1_ver = 0;
    if (mcuboot_upgrade_get_slot1_version(&slot1_ver) == 0)
    {
        off += snprintf(buf + off, sizeof(buf) - off,
                        "Slot1: %u.%u.%u\n",
                        (slot1_ver >> 24) & 0xFF, (slot1_ver >> 16) & 0xFF,
                        (slot1_ver >> 8) & 0xFF);
    }
    else
    {
        off += snprintf(buf + off, sizeof(buf) - off, "Slot1: empty\n");
    }

    off += snprintf(buf + off, sizeof(buf) - off,
                    "Pending: %s",
                    mcuboot_upgrade_has_pending() ? "yes" : "no");

    /* 弹窗 */
    lv_obj_t *mbox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(mbox, 260, 170);
    lv_obj_center(mbox);
    lv_obj_set_style_bg_color(mbox, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_border_width(mbox, 1, 0);
    lv_obj_set_style_border_color(mbox, lv_color_hex(0x808080), 0);
    lv_obj_set_style_radius(mbox, 6, 0);

    lv_obj_t *title = lv_label_create(mbox);
    lv_label_set_text(title, "Firmware Info");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *text = lv_label_create(mbox);
    lv_label_set_text(text, buf);
    lv_obj_set_style_text_color(text, lv_color_black(), 0);
    lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 35);

    lv_obj_t *ok_btn = lv_btn_create(mbox);
    lv_obj_set_size(ok_btn, 70, 30);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, 0, 5);
    lv_obj_add_event_cb(ok_btn, on_info_close_click, LV_EVENT_CLICKED, mbox);
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, "OK");
    lv_obj_center(ok_lbl);
}

/* ============================================================
 * Confirm FW 弹窗
 * ============================================================ */

static void on_confirm_yes_click(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_user_data(e);
    int ret = mcuboot_upgrade_confirm();
    if (ret == 0)
    {
        LOG_INF("Firmware confirmed permanent");
    }
    else
    {
        LOG_ERR("Confirm failed: %d", ret);
    }
    if (mbox)
        lv_obj_del(mbox);
}

static void on_confirm_no_click(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_user_data(e);
    if (mbox)
        lv_obj_del(mbox);
}

static void show_confirm_dialog(lv_event_t *e)
{
    lv_obj_t *mbox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(mbox, 260, 150);
    lv_obj_center(mbox);
    lv_obj_set_style_bg_color(mbox, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_border_width(mbox, 1, 0);
    lv_obj_set_style_border_color(mbox, lv_color_hex(0x808080), 0);
    lv_obj_set_style_radius(mbox, 6, 0);

    lv_obj_t *title = lv_label_create(mbox);
    lv_label_set_text(title, "Confirm Upgrade");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *text = lv_label_create(mbox);
    lv_label_set_text(text, "Confirm current firmware\nas permanent?");
    lv_obj_set_style_text_color(text, lv_color_black(), 0);
    lv_obj_align(text, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *yes_btn = lv_btn_create(mbox);
    lv_obj_set_size(yes_btn, 80, 30);
    lv_obj_align(yes_btn, LV_ALIGN_BOTTOM_LEFT, 20, 0);
    lv_obj_add_event_cb(yes_btn, on_confirm_yes_click, LV_EVENT_CLICKED, mbox);
    lv_obj_t *yes_lbl = lv_label_create(yes_btn);
    lv_label_set_text(yes_lbl, "Confirm");
    lv_obj_center(yes_lbl);

    lv_obj_t *cancel_btn = lv_btn_create(mbox);
    lv_obj_set_size(cancel_btn, 80, 30);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -20, 0);
    lv_obj_add_event_cb(cancel_btn, on_confirm_no_click, LV_EVENT_CLICKED, mbox);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);
}

/* ============================================================
 * 主界面构建
 * ============================================================ */

static void build_firmware_upgrade_screen(void)
{
    lv_obj_clean(g_fw_upg_scr);
    lv_obj_set_style_bg_color(g_fw_upg_scr, lv_color_hex(0x101020), 0);
    lv_obj_set_style_pad_all(g_fw_upg_scr, 0, 0);

    /* 背景图片 */
    const lv_img_dsc_t *bg_img = lvgl_sd_pic_get_image();
    if (bg_img != NULL && bg_img->data != NULL)
    {
        lv_obj_t *bg = lv_img_create(g_fw_upg_scr);
        lv_img_set_src(bg, bg_img);
        lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_img_opa(bg, LV_OPA_30, 0);
        lv_obj_move_to_index(bg, 0);
    }

    /* 标题 */
    lv_obj_t *title = lv_label_create(g_fw_upg_scr);
    lv_label_set_text(title, "Firmware Upgrade");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* ---- 按钮区域 ---- */

    /* SD 卡升级按钮 */
    lv_obj_t *sd_btn = lv_btn_create(g_fw_upg_scr);
    lv_obj_set_size(sd_btn, 220, 50);
    lv_obj_align(sd_btn, LV_ALIGN_CENTER, 0, -70);
    lv_obj_add_event_cb(sd_btn, lvgl_sd_ota_start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sd_lbl = lv_label_create(sd_btn);
    lv_label_set_text(sd_lbl, "SD Card Upgrade");
    lv_obj_center(sd_lbl);

    /* 串口升级按钮 */
    lv_obj_t *uart_btn = lv_btn_create(g_fw_upg_scr);
    lv_obj_set_size(uart_btn, 220, 50);
    lv_obj_align(uart_btn, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_event_cb(uart_btn, lvgl_uart_ota_start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *uart_lbl = lv_label_create(uart_btn);
    lv_label_set_text(uart_lbl, "UART Upgrade");
    lv_obj_center(uart_lbl);

    /* Firmware Info 按钮 */
    lv_obj_t *info_btn = lv_btn_create(g_fw_upg_scr);
    lv_obj_set_size(info_btn, 220, 50);
    lv_obj_align(info_btn, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(info_btn, show_firmware_info, LV_EVENT_CLICKED, NULL);
    lv_obj_t *info_lbl = lv_label_create(info_btn);
    lv_label_set_text(info_lbl, "Firmware Info");
    lv_obj_center(info_lbl);

    /* Confirm FW 按钮 */
    lv_obj_t *confirm_btn = lv_btn_create(g_fw_upg_scr);
    lv_obj_set_size(confirm_btn, 220, 50);
    lv_obj_align(confirm_btn, LV_ALIGN_CENTER, 0, 110);
    lv_obj_add_event_cb(confirm_btn, show_confirm_dialog, LV_EVENT_CLICKED, NULL);
    lv_obj_t *confirm_lbl = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_lbl, "Confirm FW");
    lv_obj_center(confirm_lbl);

    /* 返回按钮 */
    lv_obj_t *back_btn = lv_btn_create(g_fw_upg_scr);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    if (g_back_to_menu_cb)
    {
        lv_obj_add_event_cb(back_btn, g_back_to_menu_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_center(back_lbl);

    lv_task_handler();
}

/* ============================================================
 * 公共接口
 * ============================================================ */

/* LVGL 事件回调包装 */
void lvgl_firmware_upgrade_start_cb(lv_event_t *e)
{
    lvgl_firmware_upgrade_start();
}

int lvgl_firmware_upgrade_start(void)
{
    /* 删除旧 screen 释放内存 */
    if (g_fw_upg_scr != NULL)
    {
        lv_obj_del(g_fw_upg_scr);
        g_fw_upg_scr = NULL;
    }

    g_fw_upg_scr = lv_obj_create(NULL);

    build_firmware_upgrade_screen();
    lv_scr_load(g_fw_upg_scr);

    LOG_INF("Firmware upgrade screen started");
    return 0;
}

void lvgl_firmware_upgrade_set_back_to_menu_cb(lv_event_cb_t cb)
{
    g_back_to_menu_cb = cb;
}
