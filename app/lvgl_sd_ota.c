#include "lvgl_sd_ota.h"
#include "mcuboot_upgrade.h"
#include "lvgl_sd_pic.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/reboot.h>

#include "lvgl.h"
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(lvgl_ota, LOG_LEVEL_INF);

/* ============================================================
 * 固件包扫描
 * ============================================================ */

int ota_scan_fwpkg_list(ota_fwpkg_list_t *list)
{
    struct fs_dir_t dir;
    struct fs_dirent ent;
    int ret;

    if (!list)
        return -EINVAL;
    memset(list, 0, sizeof(*list));

    fs_dir_t_init(&dir);
    ret = fs_opendir(&dir, FWPKG_SCAN_DIR);
    if (ret != 0)
    {
        LOG_WRN("Cannot open %s (err: %d)", FWPKG_SCAN_DIR, ret);
        return -ENOENT;
    }

    while (list->count < OTA_FWPKG_LIST_MAX)
    {
        memset(&ent, 0, sizeof(ent));
        ret = fs_readdir(&dir, &ent);
        if (ret != 0 || ent.name[0] == '\0')
            break;
        if (ent.type != FS_DIR_ENTRY_FILE)
            continue;

        /* 检查扩展名 */
        const char *dot = strrchr(ent.name, '.');
        if (!dot)
            continue;
        if (strcmp(dot, ".fwpkg") != 0 && strcmp(dot, ".FWP") != 0)
            continue;

        ota_fwpkg_entry_t *e = &list->entries[list->count];
        snprintf(e->name, sizeof(e->name), "%s", ent.name);
        snprintf(e->path, sizeof(e->path), "%s/%s", FWPKG_SCAN_DIR, ent.name);
        list->count++;
    }

    fs_closedir(&dir);
    LOG_INF("Found %d fwpkg(s)", list->count);
    return 0;
}

/* ============================================================
 * OTA 升级界面
 * ============================================================ */

/* 前向声明 */
static void show_fwpkg_list_screen(void);

static lv_obj_t *g_ota_scr = NULL;
static lv_obj_t *g_ota_list = NULL;
static lv_obj_t *g_ota_status = NULL;
static lv_obj_t *g_ota_progress = NULL;
static lv_obj_t *g_ota_back_btn = NULL;
static lv_obj_t *g_ota_info_label = NULL;

static ota_fwpkg_list_t g_fwpkg_list;
static char g_selected_path[256];
static fwpkg_header_t g_selected_header;
static bool g_package_ready = false;
static bool g_upgrading = false;

/* 升级执行线程 */
static struct k_thread g_upgrade_thread_data;
static K_THREAD_STACK_DEFINE(g_upgrade_thread_stack, 4096);

/* 升级进度状态（跨线程共享） */
static volatile upgrade_state_t g_progress_state = UPGRADE_STATE_IDLE;
static volatile uint8_t g_progress_val = 0;
static volatile bool g_progress_done = false;

/* 前向声明 */
static void ota_progress_cb(upgrade_state_t state, uint8_t progress, void *user_data);

static void upgrade_thread_entry(void *arg1, void *arg2, void *arg3)
{
    int ret = mcuboot_upgrade_perform(g_selected_path, &g_selected_header,
                                      ota_progress_cb, NULL);
    if (ret != 0)
    {
        LOG_ERR("Upgrade failed: %d", ret);
        g_progress_state = UPGRADE_STATE_FAILED;
    }
    /* 由 g_upgrading=true 时 UI 刷新检查状态 */
    g_progress_done = true;
}

/* 返回主菜单回调 */
static lv_event_cb_t g_ota_back_to_menu_cb = NULL;

void lvgl_sd_ota_set_back_to_menu_cb(lv_event_cb_t cb)
{
    g_ota_back_to_menu_cb = cb;
}

/* 升级进度回调 - 只设置全局变量，不直接操作 LVGL */
static void ota_progress_cb(upgrade_state_t state, uint8_t progress, void *user_data)
{
    g_progress_state = state;
    g_progress_val = progress;
    if (state == UPGRADE_STATE_PENDING_REBOOT || state == UPGRADE_STATE_FAILED)
    {
        g_progress_done = true;
    }
}

/* LVGL 线程中刷新升级进度 UI */
static void ota_progress_ui_refresh(void)
{
    char buf[64];
    upgrade_state_t state = g_progress_state;
    uint8_t progress = g_progress_val;

    switch (state)
    {
    case UPGRADE_STATE_ERASING:
        lv_label_set_text(g_ota_status, "Erasing slot...");
        lv_bar_set_value(g_ota_progress, 0, LV_ANIM_OFF);
        break;
    case UPGRADE_STATE_WRITING:
        snprintf(buf, sizeof(buf), "Writing... %u%%", progress);
        lv_label_set_text(g_ota_status, buf);
        lv_bar_set_value(g_ota_progress, progress, LV_ANIM_ON);
        break;
    case UPGRADE_STATE_VERIFYING:
        lv_label_set_text(g_ota_status, "Verifying...");
        lv_bar_set_value(g_ota_progress, 95, LV_ANIM_ON);
        break;
    case UPGRADE_STATE_REQUESTING:
        lv_label_set_text(g_ota_status, "Requesting upgrade...");
        lv_bar_set_value(g_ota_progress, 98, LV_ANIM_ON);
        break;
    case UPGRADE_STATE_PENDING_REBOOT:
        lv_label_set_text(g_ota_status, "Upgrade SUCCESS! Rebooting...");
        lv_bar_set_value(g_ota_progress, 100, LV_ANIM_ON);
        lv_timer_handler();
        k_sleep(K_SECONDS(2));
        sys_reboot(SYS_REBOOT_COLD);
        break;
    case UPGRADE_STATE_FAILED:
        lv_label_set_text(g_ota_status, "Upgrade FAILED!");
        lv_bar_set_value(g_ota_progress, 0, LV_ANIM_OFF);
        {
            lv_obj_t *back_btn = lv_button_create(g_ota_scr);
            lv_obj_set_size(back_btn, 100, 40);
            lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
            lv_obj_add_event_cb(back_btn, (lv_event_cb_t)show_fwpkg_list_screen, LV_EVENT_CLICKED, NULL);
            lv_obj_t *back_lbl = lv_label_create(back_btn);
            lv_label_set_text(back_lbl, "Back");
            lv_obj_center(back_lbl);
        }
        g_upgrading = false;
        g_progress_done = false;
        break;
    default:
        break;
    }
}

/* 固件包信息弹窗 - 关闭回调 */
static void on_info_close_click(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_user_data(e);
    if (mbox)
    {
        lv_obj_del(mbox);
    }
}

/* 固件包信息弹窗 */
static void show_fwpkg_info(lv_event_t *e)
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

    /* 标题 */
    lv_obj_t *title = lv_label_create(mbox);
    lv_label_set_text(title, "Firmware Info");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    /* 内容 */
    lv_obj_t *text = lv_label_create(mbox);
    lv_label_set_text(text, buf);
    lv_obj_set_style_text_color(text, lv_color_black(), 0);
    lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 35);

    /* OK 按钮 - 居中排列 */
    lv_obj_t *ok_btn = lv_button_create(mbox);
    lv_obj_set_size(ok_btn, 70, 30);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, 0, 5);
    lv_obj_add_event_cb(ok_btn, on_info_close_click, LV_EVENT_CLICKED, mbox);
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, "OK");
    lv_obj_center(ok_lbl);
}

/* 确认弹窗 - Yes 回调 */
static void on_confirm_yes_click(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_user_data(e);
    int ret = mcuboot_upgrade_confirm();
    if (ret == 0)
    {
        lv_label_set_text(g_ota_status, "Firmware confirmed!");
    }
    else
    {
        lv_label_set_text(g_ota_status, "Confirm failed!");
    }
    if (mbox)
        lv_obj_del(mbox);
}

/* 确认弹窗 - No 回调 */
static void on_confirm_no_click(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_user_data(e);
    if (mbox)
        lv_obj_del(mbox);
}

/* 确认固件按钮回调 - 弹出 Yes/Cancel 对话框 */
static void on_confirm_click(lv_event_t *e)
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

    /* Confirm 按钮 */
    lv_obj_t *yes_btn = lv_button_create(mbox);
    lv_obj_set_size(yes_btn, 80, 30);
    lv_obj_align(yes_btn, LV_ALIGN_BOTTOM_LEFT, 20, 0);
    lv_obj_add_event_cb(yes_btn, on_confirm_yes_click, LV_EVENT_CLICKED, mbox);
    lv_obj_t *yes_lbl = lv_label_create(yes_btn);
    lv_label_set_text(yes_lbl, "Confirm");
    lv_obj_center(yes_lbl);

    /* Cancel 按钮 */
    lv_obj_t *cancel_btn = lv_button_create(mbox);
    lv_obj_set_size(cancel_btn, 80, 30);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -20, 0);
    lv_obj_add_event_cb(cancel_btn, on_confirm_no_click, LV_EVENT_CLICKED, mbox);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);
}

/* 升级进度界面 */
static void show_upgrade_progress_screen(void)
{
    lv_obj_clean(g_ota_scr);
    lv_obj_set_style_bg_color(g_ota_scr, lv_color_hex(0x101020), 0);
    lv_obj_set_style_pad_all(g_ota_scr, 0, 0);

    /* 背景图片 */
    const lv_image_dsc_t *bg_img = lvgl_sd_pic_get_image();
    if (bg_img != NULL && bg_img->data != NULL)
    {
        lv_obj_t *bg = lv_image_create(g_ota_scr);
        lv_image_set_src(bg, bg_img);
        lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_img_opa(bg, LV_OPA_30, 0);
        lv_obj_move_to_index(bg, 0);
    }

    lv_obj_t *title = lv_label_create(g_ota_scr);
    lv_label_set_text(title, "Upgrading...");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 固件信息 */
    g_ota_info_label = lv_label_create(g_ota_scr);
    char buf[128];
    snprintf(buf, sizeof(buf), "Ver: %u.%u.%u (build %u)",
             (g_selected_header.version >> 24) & 0xFF,
             (g_selected_header.version >> 16) & 0xFF,
             (g_selected_header.version >> 8) & 0xFF,
             g_selected_header.version & 0xFF);
    lv_label_set_text(g_ota_info_label, buf);
    lv_obj_set_style_text_color(g_ota_info_label, lv_color_white(), 0);
    lv_obj_align(g_ota_info_label, LV_ALIGN_TOP_MID, 0, 35);

    /* 状态 */
    g_ota_status = lv_label_create(g_ota_scr);
    lv_label_set_text(g_ota_status, "Starting...");
    lv_obj_set_style_text_color(g_ota_status, lv_color_white(), 0);
    lv_obj_align(g_ota_status, LV_ALIGN_CENTER, 0, -20);

    /* 进度条 */
    g_ota_progress = lv_bar_create(g_ota_scr);
    lv_obj_set_size(g_ota_progress, 300, 20);
    lv_bar_set_range(g_ota_progress, 0, 100);
    lv_bar_set_value(g_ota_progress, 0, LV_ANIM_OFF);
    lv_obj_align(g_ota_progress, LV_ALIGN_CENTER, 0, 10);

    lv_timer_handler();

    /* 执行升级 - 在独立线程中运行，不阻塞 LVGL */
    g_upgrading = true;
    k_thread_create(&g_upgrade_thread_data,
                    g_upgrade_thread_stack,
                    K_THREAD_STACK_SIZEOF(g_upgrade_thread_stack),
                    upgrade_thread_entry,
                    NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);
}

/* 固件包确认弹窗 - Confirm 回调 */
static void on_upgrade_confirm_yes(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_user_data(e);
    if (mbox)
        lv_obj_del(mbox);
    show_upgrade_progress_screen();
}

/* 固件包确认弹窗 - Cancel 回调 */
static void on_upgrade_confirm_cancel(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_user_data(e);
    if (mbox)
        lv_obj_del(mbox);
}

/* 列表项点击回调 - 弹出确认弹窗 */
static void on_fwpkg_item_click(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx < 0 || idx >= g_fwpkg_list.count)
        return;

    strncpy(g_selected_path, g_fwpkg_list.entries[idx].path, sizeof(g_selected_path) - 1);

    int ret = mcuboot_upgrade_read_header(g_selected_path, &g_selected_header);
    if (ret != 0)
    {
        lv_label_set_text(g_ota_status, "Invalid fwpkg!");
        return;
    }

    /* 弹出确认弹窗 */
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

    char buf[300];
    /* 从路径中提取文件名 */
    const char *fname = strrchr(g_selected_path, '/');
    if (fname)
        fname++;
    else
        fname = g_selected_path;
    snprintf(buf, sizeof(buf), "Upgrade firmware:\n%s?", fname);
    lv_obj_t *text = lv_label_create(mbox);
    lv_label_set_text(text, buf);
    lv_obj_set_style_text_color(text, lv_color_black(), 0);
    lv_obj_align(text, LV_ALIGN_CENTER, 0, -10);

    /* Confirm 按钮 */
    lv_obj_t *yes_btn = lv_button_create(mbox);
    lv_obj_set_size(yes_btn, 80, 30);
    lv_obj_align(yes_btn, LV_ALIGN_BOTTOM_LEFT, 20, 0);
    lv_obj_add_event_cb(yes_btn, on_upgrade_confirm_yes, LV_EVENT_CLICKED, mbox);
    lv_obj_t *yes_lbl = lv_label_create(yes_btn);
    lv_label_set_text(yes_lbl, "Confirm");
    lv_obj_center(yes_lbl);

    /* Cancel 按钮 */
    lv_obj_t *cancel_btn = lv_button_create(mbox);
    lv_obj_set_size(cancel_btn, 80, 30);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -20, 0);
    lv_obj_add_event_cb(cancel_btn, on_upgrade_confirm_cancel, LV_EVENT_CLICKED, mbox);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);
}

/* 固件包列表界面 */
static void show_fwpkg_list_screen(void)
{
    lv_obj_clean(g_ota_scr);
    lv_obj_set_style_bg_color(g_ota_scr, lv_color_hex(0x101020), 0);
    lv_obj_set_style_pad_all(g_ota_scr, 0, 0);

    /* 背景图片 */
    const lv_image_dsc_t *bg_img = lvgl_sd_pic_get_image();
    if (bg_img != NULL && bg_img->data != NULL)
    {
        lv_obj_t *bg = lv_image_create(g_ota_scr);
        lv_image_set_src(bg, bg_img);
        lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_img_opa(bg, LV_OPA_30, 0);
        lv_obj_move_to_index(bg, 0);
    }

    lv_obj_t *title = lv_label_create(g_ota_scr);
    lv_label_set_text(title, "Firmware Packages");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 扫描 */
    ota_scan_fwpkg_list(&g_fwpkg_list);

    if (g_fwpkg_list.count == 0)
    {
        g_ota_status = lv_label_create(g_ota_scr);
        lv_label_set_text(g_ota_status, "No fwpkg found in /SD:/FIRMWARE/");
        lv_obj_set_style_text_color(g_ota_status, lv_color_white(), 0);
        lv_obj_align(g_ota_status, LV_ALIGN_CENTER, 0, 0);
    }
    else
    {
        /* 列表 */
        g_ota_list = lv_list_create(g_ota_scr);
        lv_obj_set_size(g_ota_list, 400, 300);
        lv_obj_align(g_ota_list, LV_ALIGN_TOP_MID, 0, 40);

        for (int i = 0; i < g_fwpkg_list.count; i++)
        {
            lv_obj_t *btn = lv_list_add_btn(g_ota_list, NULL, g_fwpkg_list.entries[i].name);
            lv_obj_set_user_data(btn, (void *)(intptr_t)i);
            lv_obj_add_event_cb(btn, on_fwpkg_item_click, LV_EVENT_CLICKED, NULL);
        }

        g_ota_status = lv_label_create(g_ota_scr);
        char buf[64];
        snprintf(buf, sizeof(buf), "%d package(s) found", g_fwpkg_list.count);
        lv_label_set_text(g_ota_status, buf);
        lv_obj_set_style_text_color(g_ota_status, lv_color_white(), 0);
        lv_obj_align(g_ota_status, LV_ALIGN_BOTTOM_MID, 0, -60);
    }

    /* 信息按钮 */
    lv_obj_t *info_btn = lv_button_create(g_ota_scr);
    lv_obj_set_size(info_btn, 120, 35);
    lv_obj_align(info_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(info_btn, (lv_event_cb_t)show_fwpkg_info, LV_EVENT_CLICKED, NULL);
    lv_obj_t *info_lbl = lv_label_create(info_btn);
    lv_label_set_text(info_lbl, "Firmware Info");
    lv_obj_center(info_lbl);

    /* 返回按钮 */
    g_ota_back_btn = lv_button_create(g_ota_scr);
    lv_obj_set_size(g_ota_back_btn, 80, 35);
    lv_obj_align(g_ota_back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    if (g_ota_back_to_menu_cb)
    {
        lv_obj_add_event_cb(g_ota_back_btn, g_ota_back_to_menu_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *back_lbl = lv_label_create(g_ota_back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_center(back_lbl);

    /* 确认固件按钮 - 右下角，始终可见 */
    lv_obj_t *confirm_btn = lv_button_create(g_ota_scr);
    lv_obj_set_size(confirm_btn, 120, 35);
    lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_event_cb(confirm_btn, on_confirm_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *confirm_lbl = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_lbl, "Confirm FW");
    lv_obj_center(confirm_lbl);
}

/* 设置返回按钮回调（供外部调用） */
void lvgl_sd_ota_set_back_cb(lv_event_cb_t cb)
{
    if (g_ota_back_btn)
    {
        lv_obj_add_event_cb(g_ota_back_btn, cb, LV_EVENT_CLICKED, NULL);
    }
}

/* ============================================================
 * 公共接口
 * ============================================================ */

int lvgl_sd_ota_start(void)
{
    /* 删除旧 screen 释放内存 */
    if (g_ota_scr != NULL)
    {
        lv_obj_del(g_ota_scr);
        g_ota_scr = NULL;
    }

    g_ota_scr = lv_obj_create(NULL);
    g_package_ready = false;
    g_upgrading = false;

    show_fwpkg_list_screen();
    lv_screen_load(g_ota_scr);

    return 0;
}

/* LVGL 事件回调包装 */
void lvgl_sd_ota_start_cb(lv_event_t *e)
{
    lvgl_sd_ota_start();
}

void *lvgl_sd_ota_create_screen(void)
{
    lvgl_sd_ota_start();
    return g_ota_scr;
}

/* LVGL 主循环中调用，刷新升级进度 */
void lvgl_sd_ota_progress_refresh(void)
{
    if (g_upgrading)
    {
        ota_progress_ui_refresh();
    }
}
