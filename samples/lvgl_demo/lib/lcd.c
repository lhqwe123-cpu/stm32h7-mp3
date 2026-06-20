#include "lcd.h"
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include <string.h>
#include "lvgl.h"

LOG_MODULE_REGISTER(lcd, LOG_LEVEL_INF);

extern lv_disp_drv_t disp_drv;
const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
uint16_t *fr_addr = (uint16_t *)(0xC0000000UL);
uint16_t *fb_addr = (uint16_t *)(0xC00BB800UL);
uint16_t *ltdc_ptr[2];

/* DMA2D 传输完成标志 */
static volatile bool dma2d_tc = true;

struct display_buffer_descriptor disp_desc = {
    .width = LCD_W,
    .height = LCD_H,
    .pitch = LCD_W,
    .buf_size = (size_t)LCD_W * LCD_H * sizeof(uint16_t),
};

int lcd_init(void)
{
    ltdc_ptr[0] = fr_addr;
    ltdc_ptr[1] = fb_addr;
    dma2d_init();
    LOG_INF("initialized, resolution %dx%d", LCD_W, LCD_H);
    return 1;
}

/* ── DMA2D 中断服务函数（Zephyr IRQ_CONNECT 绑定） ── */
static void dma2d_tc_cb(const void *arg)
{
    ARG_UNUSED(arg);

    if (DMA2D->ISR & DMA2D_FLAG_TC)
    {
        DMA2D->IFCR |= DMA2D_FLAG_TC; /* 清除 TC 标志 */
        dma2d_tc = true;              /* 通知传输完成 */
        lv_disp_flush_ready(&disp_drv);
    }

    if (DMA2D->ISR & DMA2D_FLAG_TE)
    {
        DMA2D->IFCR |= DMA2D_FLAG_TE; /* 清除 TE 标志 */
        dma2d_tc = true;
        LOG_ERR("DMA2D transfer error");
    }
}

/* ── DMA2D 中断初始化 ── */
int dma2d_init(void)
{
    __HAL_RCC_DMA2D_CLK_ENABLE();

    /* Zephyr 中断连接 */
    IRQ_CONNECT(DMA2D_IRQn, 5, dma2d_tc_cb, NULL, 0);
    irq_enable(DMA2D_IRQn);

    LOG_INF("DMA2D interrupt initialized (Zephyr IRQ_CONNECT)");
    return 0;
}

/* LVGL flush 回调：DMA2D 中断模式写入 framebuffer   */
int lcd_flush(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    uint32_t dest_w = ex - sx + 1;
    uint32_t dest_h = ey - sy + 1;
    uint32_t offline = LCD_W - dest_w;
    uint32_t addr = (uint32_t)fr_addr + 2 * (LCD_W * sy + sx);

    /* 等待上一次传输完成 */
    while (!dma2d_tc)
        ;
    dma2d_tc = false;

    /* 配置 DMA2D 寄存器并启动（使能 TC 中断） */
    DMA2D->CR &= ~(DMA2D_CR_START);
    DMA2D->CR = DMA2D_M2M | DMA2D_CR_TCIE; /* M2M + 传输完成中断 */
    DMA2D->FGPFCCR = DMA2D_OUTPUT_RGB565;
    DMA2D->FGOR = 0;
    DMA2D->OOR = offline;
    DMA2D->FGMAR = (uint32_t)color;
    DMA2D->OMAR = addr;
    DMA2D->NLR = (dest_w << DMA2D_NLR_PL_Pos) | (dest_h << DMA2D_NLR_NL_Pos);

    DMA2D->CR |= DMA2D_CR_START;

    return 0;
}

int lcd_swap(void)
{
    display_write(disp, 0, 0, &disp_desc, fr_addr);

    return 0;
}
