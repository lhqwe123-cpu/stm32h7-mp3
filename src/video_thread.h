#ifndef __VIDEO_THREAD_H__
#define __VIDEO_THREAD_H__

#include <zephyr/kernel.h>

/* video 线程栈大小 */
#define VIDEO_THREAD_STACK_SIZE 32768

/* video 线程优先级 */
#define VIDEO_THREAD_PRIORITY 7

/* lv_timer_handler 调用间隔（毫秒） */
#define LVGL_HANDLER_INTERVAL_MS 5

/* 获取 video 线程 ID */
k_tid_t video_thread_get_id(void);

/* video 线程初始化（创建线程但不启动） */
int video_thread_init(void);

/* 启动 video 线程 */
void video_thread_start(void);

#endif /* __VIDEO_THREAD_H__ */
