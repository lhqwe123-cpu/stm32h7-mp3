#ifndef __VIDEO_THREAD_H__
#define __VIDEO_THREAD_H__

#include <zephyr/kernel.h>

/* 视频播放线程栈大小 */
#define VIDEO_THREAD_STACK_SIZE 32768

/* 视频播放线程优先级 */
#define VIDEO_THREAD_PRIORITY 5

/* 获取视频播放线程ID（供外部使用） */
k_tid_t video_thread_get_id(void);

/* 视频播放线程初始化（创建线程，但不启动） */
int video_thread_init(void);

/* 启动视频播放线程 */
void video_thread_start(void);

#endif /* __VIDEO_THREAD_H__ */
