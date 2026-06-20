/**
 * @file lv_port_disp_templ.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include <zephyr/logging/log.h>
#include "lv_port_disp_template.h"
#include "../../lvgl.h"
/* 引入LCD底层驱动 */
#include "lcd.h"

LOG_MODULE_REGISTER(lv_port_disp, LOG_LEVEL_INF);

/*********************
 *      DEFINES
 *********************/
#define USE_SRAM 0 /* 是否使用SRAM作为LVGL缓冲区，1表示使用，0表示不使用 */
#ifdef USE_SRAM
#endif

#define MY_DISP_HOR_RES (800) /* 显示屏水平分辨率 */
#define MY_DISP_VER_RES (480) /* 显示屏垂直分辨率 */

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
/* 显示屏初始化函数（前置声明） */
static void disp_init(void);
/* LVGL刷新回调函数 */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
// static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//         const lv_area_t * fill_area, lv_color_t color);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
/*
显示屏初始化入口 lv_port_disp_init
完成LVGL显示驱动的注册和配置
由主程序调用一次即可
*/
#define LVGL_BUF_SIZE (LCD_W * LCD_H / 10)
lv_disp_drv_t disp_drv;
static lv_color_t lvgl_buf1[LVGL_BUF_SIZE];
static lv_color_t lvgl_buf2[LVGL_BUF_SIZE];

void lv_port_disp_init(void)
{
    disp_init();

    static lv_disp_draw_buf_t draw_buf_dsc;
    lv_disp_draw_buf_init(&draw_buf_dsc, lvgl_buf1, lvgl_buf2, LVGL_BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf_dsc;
    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*
显示屏硬件初始化 disp_init
在此函数中完成LCD控制器的初始化配置
由 lv_port_disp_init 内部调用
*/
static void disp_init(void)
{
    /*You code here*/
}

/*Flush the content of the internal buffer the specific area on the display
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_disp_flush_ready()' has to be called when finished.*/

/*
LVGL刷新回调 disp_flush
将LVGL绘制缓冲区的内容刷新到显示屏指定区域
参数说明：
      disp_drv    : LVGL显示驱动对象
      area        : 需要刷新的矩形区域（包含起始和结束坐标）
      color_p     : 像素颜色数据缓冲区指针
可以使用 DMA 或其他硬件加速在后台完成此操作，
      但完成后必须调用 'lv_disp_flush_ready()'
*/

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    sys_cache_data_flush_range((uint32_t *)color_p, (size_t)lv_area_get_size(area) * sizeof(lv_color_t));
    lcd_flush(area->x1, area->y1, area->x2, area->y2, (uint16_t *)color_p);
    if (lv_disp_flush_is_last(disp_drv))
        lcd_swap();
}

#else /*Enable this file at the top*/

#endif
