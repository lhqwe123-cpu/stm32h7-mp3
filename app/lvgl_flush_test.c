#include "lvgl_flush_test.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "lvgl.h"

LOG_MODULE_REGISTER(lvgl_flush, LOG_LEVEL_INF);

#define FLUSH_TIMER_PERIOD_MS 100
/* 测试页面 */
static lv_obj_t *g_flush_scr = NULL;
static lv_timer_t *g_flush_timer = NULL;
static lv_event_cb_t g_flush_back_cb = NULL;
static int g_color_idx = 0;

/* 颜色循环表 */
static const lv_color_t g_colors[] = {
        LV_COLOR_MAKE(0xFF, 0x00, 0x00), /* 红 */
        LV_COLOR_MAKE(0x00, 0xFF, 0x00), /* 绿 */
        LV_COLOR_MAKE(0x00, 0x00, 0xFF), /* 蓝 */
        LV_COLOR_MAKE(0xFF, 0xFF, 0x00), /* 黄 */
        LV_COLOR_MAKE(0xFF, 0x00, 0xFF), /* 紫 */
        LV_COLOR_MAKE(0x00, 0xFF, 0xFF), /* 青 */
        LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), /* 白 */
        LV_COLOR_MAKE(0x80, 0x80, 0x80), /* 灰 */
};
#define COLOR_COUNT (sizeof(g_colors) / sizeof(g_colors[0]))

/* 定时器回调 - 切换全屏颜色 */
static void flush_timer_cb(lv_timer_t *timer)
{
    if (g_flush_scr == NULL)
        return;

    g_color_idx = (g_color_idx + 1) % COLOR_COUNT;
    lv_obj_set_style_bg_color(g_flush_scr, g_colors[g_color_idx], 0);
    lv_obj_set_style_bg_opa(g_flush_scr, LV_OPA_COVER, 0);
}

/* 返回按钮回调 */
static void on_flush_back_click(lv_event_t *e)
{
        /* 停止定时器 */
    if (g_flush_timer)
    {
        lv_timer_del(g_flush_timer);
        g_flush_timer = NULL;
    }

        /* 删除页面释放内存 */
    if (g_flush_scr)
    {
        lv_obj_del(g_flush_scr);
        g_flush_scr = NULL;
    }

        /* 返回主菜单 */
    if (g_flush_back_cb)
    {
        g_flush_back_cb(e);
    }
}

void lvgl_flush_test_set_back_cb(lv_event_cb_t cb)
{
    g_flush_back_cb = cb;
}

int lvgl_flush_test_start(void)
{
        /* 删除旧页面 */
    if (g_flush_scr)
    {
        lv_obj_del(g_flush_scr);
        g_flush_scr = NULL;
    }
    if (g_flush_timer)
    {
        lv_timer_del(g_flush_timer);
        g_flush_timer = NULL;
    }

        /* 创建全屏测试页面 */
    g_flush_scr = lv_obj_create(NULL);
    lv_obj_set_size(g_flush_scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_flush_scr, g_colors[0], 0);
    lv_obj_set_style_bg_opa(g_flush_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_flush_scr, 0, 0);
    lv_obj_set_style_pad_all(g_flush_scr, 0, 0);

        /* 标题 */
    lv_obj_t *title = lv_label_create(g_flush_scr);
    lv_label_set_text(title, "Flush Test");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

        /* 帧率提示 */
    lv_obj_t *hint = lv_label_create(g_flush_scr);
    lv_label_set_text(hint, "Full screen color cycling...");
    lv_obj_set_style_text_color(hint, lv_color_white(), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);

        /* 返回按钮 - 左下角 */
    lv_obj_t *back_btn = lv_button_create(g_flush_scr);
    lv_obj_set_size(back_btn, 80, 35);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(back_btn, on_flush_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_center(back_lbl);

        /* 启动定时器 - 每 FLUSH_TIMER_PERIOD_MS 切换颜色 */
    g_flush_timer = lv_timer_create(flush_timer_cb, FLUSH_TIMER_PERIOD_MS, NULL);

    lv_screen_load(g_flush_scr);
    g_color_idx = 0;

    LOG_INF("flush test started");
    return 0;
}
