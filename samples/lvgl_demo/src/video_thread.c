#include "video_thread.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>

LOG_MODULE_REGISTER(video_thread, LOG_LEVEL_INF);

#include "lvgl.h"
#include "lv_port_indev_template.h"
#include "lv_port_disp_template.h"

#include "lcd.h"
#include "my_malloc.h"
#include "gt1151q.h"
#include "lvgl_sd_video.h"
#include "lvgl_sd_pic.h"
#include "lvgl_sd_ota.h"
#include "lvgl_uart_ota.h"
#include "lvgl_firmware_upgrade.h"
#include "lvgl_flush_test.h"
#include "jpeg_app.h"

/* 线程栈和线程数据结构（栈放置在 DTCM 区域） */
__attribute__((__section__("DTCM"))) static char video_thread_stack[VIDEO_THREAD_STACK_SIZE];
static struct k_thread video_thread_data;
static k_tid_t video_thread_id;

/* LVGL 定时器（1ms tick） */
static struct k_timer lvgl_tick_timer;

/* 主菜单 screen */
static lv_obj_t *g_main_menu_scr = NULL;

/* LVGL tick 回调 */
static void lvgl_tick_callback(struct k_timer *timer)
{
    lv_tick_inc(1);
}

/* ============================================================
 * 主菜单界面
 * ============================================================ */

static void show_main_menu(void)
{
    /* 删除旧主菜单释放内存 */
    if (g_main_menu_scr != NULL)
    {
        lv_obj_del(g_main_menu_scr);
        g_main_menu_scr = NULL;
    }

    g_main_menu_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_main_menu_scr, lv_color_hex(0x101020), 0);
    lv_obj_set_style_pad_all(g_main_menu_scr, 0, 0);

    /* 背景图片 */
    const lv_img_dsc_t *bg_img = lvgl_sd_pic_get_image();
    if (bg_img != NULL && bg_img->data != NULL)
    {
        lv_obj_t *bg = lv_img_create(g_main_menu_scr);
        lv_img_set_src(bg, bg_img);
        lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_img_opa(bg, LV_OPA_30, 0);
        lv_obj_move_to_index(bg, 0);
    }

    /* 标题 */
    lv_obj_t *title = lv_label_create(g_main_menu_scr);
    lv_label_set_text(title, "Main Menu");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* 固件升级界面按钮 (统一入口) */
    lv_obj_t *fw_btn = lv_btn_create(g_main_menu_scr);
    lv_obj_set_size(fw_btn, 200, 50);
    lv_obj_align(fw_btn, LV_ALIGN_CENTER, 0, -60);
    lv_obj_add_event_cb(fw_btn, (lv_event_cb_t)lvgl_firmware_upgrade_start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *fw_lbl = lv_label_create(fw_btn);
    lv_label_set_text(fw_lbl, "Firmware Upgrade");
    lv_obj_center(fw_lbl);

    /* 视频播放界面按钮 */
    lv_obj_t *video_btn = lv_btn_create(g_main_menu_scr);
    lv_obj_set_size(video_btn, 200, 50);
    lv_obj_align(video_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(video_btn, (lv_event_cb_t)lvgl_sd_video_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *video_lbl = lv_label_create(video_btn);
    lv_label_set_text(video_lbl, "Video Player");
    lv_obj_center(video_lbl);

    /* 刷新测试按钮 */
    lv_obj_t *flush_btn = lv_btn_create(g_main_menu_scr);
    lv_obj_set_size(flush_btn, 200, 50);
    lv_obj_align(flush_btn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(flush_btn, (lv_event_cb_t)lvgl_flush_test_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *flush_lbl = lv_label_create(flush_btn);
    lv_label_set_text(flush_lbl, "Flush Test");
    lv_obj_center(flush_lbl);

    lv_scr_load(g_main_menu_scr);
}

/* ============================================================
 * 视频播放线程入口
 * ============================================================ */

static void video_thread_entry(void *arg1, void *arg2, void *arg3)
{
    LOG_INF("started");

    /* 启动 LVGL 1ms tick 定时器 */
    k_timer_start(&lvgl_tick_timer, K_MSEC(1), K_MSEC(1));

    /* 注册视频界面的返回主菜单回调 */
    lvgl_sd_video_set_back_to_menu_cb((lv_event_cb_t)show_main_menu);

    /* 注册 OTA 界面的返回回调 - 返回到统一固件升级界面 */
    lvgl_sd_ota_set_back_to_menu_cb((lv_event_cb_t)lvgl_firmware_upgrade_start_cb);

    /* 注册串口 OTA 界面的返回回调 - 返回到统一固件升级界面 */
    lvgl_uart_ota_set_back_to_menu_cb((lv_event_cb_t)lvgl_firmware_upgrade_start_cb);

    /* 注册统一固件升级界面的返回主菜单回调 */
    lvgl_firmware_upgrade_set_back_to_menu_cb((lv_event_cb_t)show_main_menu);

    /* 注册刷新测试页面的返回主菜单回调 */
    lvgl_flush_test_set_back_cb((lv_event_cb_t)show_main_menu);

    /* 显示主菜单 */
    show_main_menu();

    /* LVGL 主循环 */
    while (1)
    {
        lv_task_handler();
        lvgl_sd_ota_progress_refresh();
        lvgl_uart_ota_progress_refresh();
        k_sleep(K_MSEC(2));
    }
}

/* 视频播放线程初始化（由主线程调用，完成所有资源初始化） */
int video_thread_init(void)
{
    /* LCD 初始化 */
    lcd_init();

    /* 显示开机图片 */
    jpeg_show_first_picture();
    display_blanking_off(disp);

    /* 触摸屏初始化 */
    GT1151Q_Init();

    /* LVGL 初始化 */
    lv_init();
    lv_fs_fatfs_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* 初始化 LVGL tick 定时器 */
    k_timer_init(&lvgl_tick_timer, lvgl_tick_callback, NULL);

    /* 创建线程（暂不启动） */
    video_thread_id = k_thread_create(&video_thread_data,
                                      (k_thread_stack_t *)video_thread_stack,
                                      VIDEO_THREAD_STACK_SIZE,
                                      video_thread_entry,
                                      NULL, NULL, NULL,
                                      VIDEO_THREAD_PRIORITY,
                                      0,
                                      K_FOREVER);

    if (video_thread_id == NULL)
    {
        LOG_ERR("create failed");
        return -1;
    }

    LOG_INF("initialized");
    return 0;
}

/* 启动视频播放线程 */
void video_thread_start(void)
{
    k_thread_start(video_thread_id);
}

/* 获取线程ID */
k_tid_t video_thread_get_id(void)
{
    return video_thread_id;
}
