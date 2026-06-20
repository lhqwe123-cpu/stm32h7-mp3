#ifndef __LVGL_SD_PIC_H__
#define __LVGL_SD_PIC_H__

#include "lvgl.h"

/* 显示SD卡图片（独立屏幕） */
void lvgl_sd_picture_show(void);

/* 获取SD卡解码后的图片数据（用于其他界面作为背景）
  * 返回 lv_image_dsc_t 指针，失败返回 NULL
  * 注意：返回的指针由 lvgl_sd_pic 模块管理，调用者不应释放 */
const lv_image_dsc_t *lvgl_sd_pic_get_image(void);

/* 释放 lvgl_sd_pic 模块持有的图片资源 */
void lvgl_sd_pic_release(void);

#endif /* __LVGL_SD_PIC_H__ */
