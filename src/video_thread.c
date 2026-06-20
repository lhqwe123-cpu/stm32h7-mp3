#include "video_thread.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <zephyr/devicetree.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include "lcd.h"
#include "gt1151q.h"
#include "my_malloc.h"
#include "jpegcodec.h"
#include "lvgl_sd_video.h"
#include "lvgl_sd_pic.h"
#include "lvgl_sd_ota.h"
#include "lvgl_uart_ota.h"
#include "lvgl_firmware_upgrade.h"
#include "lvgl_flush_test.h"

LOG_MODULE_REGISTER(video_thread, LOG_LEVEL_INF);

/* 线程栈和线程数据结构（栈放置在 DTCM 区域） */
__attribute__((__section__("DTCM"))) static char video_thread_stack[VIDEO_THREAD_STACK_SIZE];
static struct k_thread video_thread_data;
static k_tid_t video_thread_id;

/* ── 触摸轮询间隔（毫秒），独立于 LVGL 刷新周期 ────────────── */
#define TOUCH_POLL_INTERVAL_MS 20

/* ── LVGL 触摸 indev 读回调 ────────────────────────────────── */
static lv_indev_t *touch_indev;
static uint32_t last_touch_read_ms;
static lv_coord_t last_x;
static lv_coord_t last_y;
static lv_indev_state_t last_state;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    GT1151Q_Data_t *touch = (GT1151Q_Data_t *)lv_indev_get_user_data(indev);

    /* 节流：控制实际 I2C 读取频率 */
    uint32_t now = lv_tick_get();
    if (now - last_touch_read_ms < TOUCH_POLL_INTERVAL_MS)
    {
        /* 未到轮询间隔，返回上一次状态（保证滑动连续） */
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = last_state;
        return;
    }
    last_touch_read_ms = now;

    if (GT1151Q_ReadData(touch) == GT1151Q_OK && touch->count > 0U)
    {
        GT1151Q_TransformToScreen(touch);
        last_x = (lv_coord_t)touch->points[0].x;
        last_y = (lv_coord_t)touch->points[0].y;
        last_state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        last_state = LV_INDEV_STATE_RELEASED;
    }

    data->point.x = last_x;
    data->point.y = last_y;
    data->state = last_state;
}

/* ── 触摸初始化 ──*/
static void touch_init(void)
{
    GT1151Q_Status_e st = GT1151Q_Init();
    if (st != GT1151Q_OK)
    {
        LOG_ERR("GT1151Q init failed: %d", st);
        return;
    }

    /* 读取产品 ID 确认设备 */
    char pid[5];
    if (GT1151Q_ReadProductID(pid) == GT1151Q_OK)
    {
        LOG_INF("GT1151Q product ID: %c%c%c%c", pid[0], pid[1], pid[2], pid[3]);
    }

    /* 创建 LVGL pointer indev */
    touch_indev = lv_indev_create();
    if (touch_indev == NULL)
    {
        LOG_ERR("lv_indev_create failed");
        return;
    }

    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, touch_read_cb);
    lv_indev_set_user_data(touch_indev, (void *)&Touch_dev);

    /* 绑定到默认 display */
    lv_indev_set_display(touch_indev, lv_display_get_default());

    LOG_INF("GT1151Q touch indev registered");
}

/* ── 主菜单 screen ────────────────────────────────────────── */
static lv_obj_t *g_main_menu_scr = NULL;

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
    const lv_image_dsc_t *bg_img = lvgl_sd_pic_get_image();
    if (bg_img != NULL && bg_img->data != NULL)
    {
        lv_obj_t *bg = lv_image_create(g_main_menu_scr);
        lv_image_set_src(bg, bg_img);
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
    lv_obj_t *fw_btn = lv_button_create(g_main_menu_scr);
    lv_obj_set_size(fw_btn, 200, 50);
    lv_obj_align(fw_btn, LV_ALIGN_CENTER, 0, -60);
    lv_obj_add_event_cb(fw_btn, (lv_event_cb_t)lvgl_firmware_upgrade_start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *fw_lbl = lv_label_create(fw_btn);
    lv_label_set_text(fw_lbl, "Firmware Upgrade");
    lv_obj_center(fw_lbl);

    /* 视频播放界面按钮 */
    lv_obj_t *video_btn = lv_button_create(g_main_menu_scr);
    lv_obj_set_size(video_btn, 200, 50);
    lv_obj_align(video_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(video_btn, (lv_event_cb_t)lvgl_sd_video_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *video_lbl = lv_label_create(video_btn);
    lv_label_set_text(video_lbl, "Video Player");
    lv_obj_center(video_lbl);

    /* 刷新测试按钮 */
    lv_obj_t *flush_btn = lv_button_create(g_main_menu_scr);
    lv_obj_set_size(flush_btn, 200, 50);
    lv_obj_align(flush_btn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(flush_btn, (lv_event_cb_t)lvgl_flush_test_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *flush_lbl = lv_label_create(flush_btn);
    lv_label_set_text(flush_lbl, "Flush Test");
    lv_obj_center(flush_lbl);

    lv_screen_load(g_main_menu_scr);
}

/* video 线程入口函数 */
static void video_thread_entry(void *arg1, void *arg2, void *arg3)
{
    LOG_INF("video thread started");

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
        lv_timer_handler();
        lvgl_sd_ota_progress_refresh();
        lvgl_uart_ota_progress_refresh();
        k_msleep(LVGL_HANDLER_INTERVAL_MS);
    }
}

/* video 线程初始化 */
int video_thread_init(void)
{
    const struct device *display_dev;

    /* 获取显示设备 */
    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev))
    {
        LOG_ERR("display device not ready");
        return -1;
    }

    /* 打开 LCD 显示（Zephyr LTDC + LVGL 驱动已接管 framebuffer） */
    display_blanking_off(display_dev);

    /* 初始化触摸 */
    touch_init();

    /* 创建线程（暂不启动，K_FOREVER 挂起） */
    video_thread_id = k_thread_create(&video_thread_data,
                                      (k_thread_stack_t *)video_thread_stack,
                                      VIDEO_THREAD_STACK_SIZE,
                                      video_thread_entry,
                                      (void *)display_dev, NULL, NULL,
                                      VIDEO_THREAD_PRIORITY,
                                      0,
                                      K_FOREVER);

    if (video_thread_id == NULL)
    {
        LOG_ERR("create failed");
        return -1;
    }

    /* 设置线程名（调试/Shell 中可见） */
    k_thread_name_set(video_thread_id, "video");

    LOG_INF("initialized");
    return 0;
}

/* 启动 video 线程 */
void video_thread_start(void)
{
    k_thread_start(video_thread_id);
}

/* 获取线程 ID */
k_tid_t video_thread_get_id(void)
{
    return video_thread_id;
}
