#include "lcd.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lcd, LOG_LEVEL_INF);

/* LCD 分辨率常量，由 Zephyr LTDC + LVGL 驱动统一管理 */
