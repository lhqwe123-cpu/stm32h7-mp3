#ifndef __LVGL_SD_VIDEO_H__
#define __LVGL_SD_VIDEO_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* 视频列表最大条目数 */
#define SD_VIDEO_LIST_MAX 64

/* 视频文件信息 */
typedef struct
{
        char name[256]; /* 文件名 */
        char path[280]; /* 完整路径 */
} sd_video_entry_t;

/* 视频列表 */
typedef struct
{
    sd_video_entry_t entries[SD_VIDEO_LIST_MAX];
    int count;
} sd_video_list_t;

/* 启动视频播放应用（先显示列表界面） */
int lvgl_sd_video_start(void);

/* 扫描SD卡视频目录，获取视频文件列表 */
int sd_video_scan_list(sd_video_list_t *list);

/**
  * @brief 设置返回主菜单的回调函数
  * 当用户在视频播放界面点击返回时调用
 */
void lvgl_sd_video_set_back_to_menu_cb(lv_event_cb_t cb);

#endif /* __LVGL_SD_VIDEO_H__ */
