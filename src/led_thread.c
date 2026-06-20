#include "led_thread.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(led_thread, LOG_LEVEL_INF);

/* LED 设备树节点 */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

/* 线程栈和线程数据结构（栈放置在 DTCM 区域） */
__attribute__((__section__("DTCM"))) static char led_thread_stack[LED_THREAD_STACK_SIZE];
static struct k_thread led_thread_data;
static k_tid_t led_thread_id;

/* LED 设备规格 */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* LED闪烁线程入口函数 */
static void led_thread_entry(void *arg1, void *arg2, void *arg3)
{
    bool led_state = true;

    LOG_INF("started");

    while (1)
    {
        /* 翻转 LED0 和 LED1 */
        led_state = !led_state;

        gpio_pin_set_dt(&led0, led_state ? 1 : 0);

        k_sleep(K_MSEC(LED_BLINK_INTERVAL_MS));
    }
}

/* LED闪烁线程初始化（含GPIO初始化） */
int led_thread_init(void)
{
    /* 初始化 GPIO */
    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);

    /* 创建线程（暂不启动） */
    led_thread_id = k_thread_create(&led_thread_data,
                                    (k_thread_stack_t *)led_thread_stack,
                                    LED_THREAD_STACK_SIZE,
                                    led_thread_entry,
                                    NULL, NULL, NULL,
                                    LED_THREAD_PRIORITY,
                                    0,
                                    K_FOREVER);

    if (led_thread_id == NULL)
    {
        LOG_ERR("create failed");
        return -1;
    }

    /* 设置线程名（调试/Shell 中可见） */
    k_thread_name_set(led_thread_id, "led");

    LOG_INF("initialized");
    return 0;
}

/* 启动LED闪烁线程 */
void led_thread_start(void)
{
    k_thread_start(led_thread_id);
}

/* 获取线程ID */
k_tid_t led_thread_get_id(void)
{
    return led_thread_id;
}
