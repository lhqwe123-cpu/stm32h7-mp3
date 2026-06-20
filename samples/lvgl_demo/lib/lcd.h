#ifndef __LCD_H__
#define __LCD_H__

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/cache.h>
#include <stdint.h>

extern const struct device *disp;
extern uint16_t *fr_addr;
extern uint16_t *fb_addr;

/* RGB565 颜色宏 */
#define RGB565(r, g, b) ((((uint16_t)(r) & 0xF8) << 8) | \
                         (((uint16_t)(g) & 0xFC) << 3) | \
                         (((uint16_t)(b) & 0xF8) >> 3))
#define LCD_W 800U
#define LCD_H 480U
#define LCD_PIXSIZE 2U /* RGB565 每像素2字节 */
/**
 * lcd_flush  使用 DMA2D 将 LVGL 渲染结果直接写入 framebuffer
 * @param sx, sy: 区域起始坐标
 * @param ex, ey: 区域结束坐标
 * @param color:  源数据指针 (LVGL 的 RGB565 buffer)
 * @return 0 成功
 */
int lcd_flush(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);
int lcd_init(void);
int lcd_swap(void);

/* DMA2D 中断初始化（Zephyr IRQ_CONNECT 方式） */
int dma2d_init(void);
#endif /* __LCD_H__ */
