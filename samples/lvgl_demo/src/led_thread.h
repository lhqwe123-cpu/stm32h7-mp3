#ifndef __LED_THREAD_H__
#define __LED_THREAD_H__

#include <zephyr/kernel.h>

/* LED闪烁线程栈大小 */
#define LED_THREAD_STACK_SIZE 1024

/* LED闪烁线程优先级 */
#define LED_THREAD_PRIORITY 7

/* LED闪烁间隔（毫秒） */
#define LED_BLINK_INTERVAL_MS 500

/* 获取LED线程ID */
k_tid_t led_thread_get_id(void);

/* LED闪烁线程初始化（含GPIO初始化，创建线程但不启动） */
int led_thread_init(void);

/* 启动LED闪烁线程 */
void led_thread_start(void);

#endif /* __LED_THREAD_H__ */
