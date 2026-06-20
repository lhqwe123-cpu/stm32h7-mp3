#include "my_malloc.h"
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <zephyr/cache.h>

/* AXI堆：让 Zephyr 自动放置 */
K_HEAP_DEFINE(sram_heap, SRAM_HEAP_SIZE);

// static struct k_heap sram_heap;   // 内部SRAM内存池
static struct k_heap sdram_heap;  /* 外部SDRAM内存池 (非static, 供外部重新初始化) */
static struct k_heap sram12_heap; // 内部SRAM1+SRAM2内存池
static struct k_heap sram4_heap;  // 内部SRAM4内存池
static struct k_heap dtcm_heap;   // 内部DTCM内存池
static struct k_heap itcm_heap;   // 内部ITCM内存池

void my_heap_init(void)
{
    // k_heap_init(&sram_heap, (void *)SRAM_HEAP_BASE, SRAM_HEAP_SIZE);
    k_heap_init(&sdram_heap, (void *)SDRAM_HEAP_BASE, SDRAM_HEAP_SIZE);
    /* 刷新 Cache 确保堆元数据写入物理 SDRAM (只需刷新元数据区域) */
    sys_cache_data_flush_range((void *)SDRAM_HEAP_BASE, 4096);
    k_heap_init(&sram12_heap, (void *)SRAM12_HEAP_BASE, SRAM12_HEAP_SIZE);
    k_heap_init(&sram4_heap, (void *)SRAM3_HEAP_BASE, SRAM3_HEAP_SIZE);
    k_heap_init(&dtcm_heap, (void *)DICM_HEAP_BASE, DICM_HEAP_SIZE);
    k_heap_init(&itcm_heap, (void *)ITCM_HEAP_BASE, ITCM_HEAP_SIZE);

    void *heap_start = sram_heap.heap.init_mem;

    printk("sram: heap at %p, %uKB\n",
           heap_start,
           (unsigned int)(SRAM_HEAP_SIZE / (1024U)));
    printk("sdram: heap at 0x%08lx, %uMB\n",
           (unsigned long)SDRAM_HEAP_BASE,
           (unsigned int)(SDRAM_HEAP_SIZE / (1024U * 1024U)));
    printk("sram12: heap at 0x%08lx, %uKB\n",
           (unsigned long)SRAM12_HEAP_BASE,
           (unsigned int)(SRAM12_HEAP_SIZE / (1024U)));
    printk("sram4: heap at 0x%08lx, %uKB\n",
           (unsigned long)SRAM3_HEAP_BASE,
           (unsigned int)(SRAM3_HEAP_SIZE / (1024U)));
    printk("dtcm: heap at 0x%08lx, %uKB\n",
           (unsigned long)DICM_HEAP_BASE,
           (unsigned int)(DICM_HEAP_SIZE / (1024U)));
    printk("itcm: heap at 0x%08lx, %uKB\n",
           (unsigned long)ITCM_HEAP_BASE,
           (unsigned int)(ITCM_HEAP_SIZE / (1024U)));
}

void *mymalloc(uint8_t memx, uint32_t size)
{
    void *ptr = NULL;

    switch (memx)
    {
    case SRAMIN:
        ptr = k_heap_alloc(&sram_heap, size, K_NO_WAIT);
        break;
    case SRAMEX:
        ptr = k_heap_alloc(&sdram_heap, size, K_NO_WAIT);
        break;
    case SRAM12:
        ptr = k_heap_alloc(&sram12_heap, size, K_NO_WAIT);
        break;
    case SRAM4:
        ptr = k_heap_alloc(&sram4_heap, size, K_NO_WAIT);
        break;
    case SRAMDTCM:
        ptr = k_heap_alloc(&dtcm_heap, size, K_NO_WAIT);
        break;
    case SRAMITCM:
        ptr = k_heap_alloc(&itcm_heap, size, K_NO_WAIT);
        break;
    default:
        break;
    }

    if (ptr == NULL)
    {
        printk("malloc FAIL! memx=%u size=%u\n", memx, size);
    }

    return ptr;
}

void myfree(uint8_t memx, void *ptr)
{
    if (ptr == NULL)
    {
        printk("double FREE! memx=%u\n", memx);
        return;
    }

    switch (memx)
    {
    case SRAMIN:
        k_heap_free(&sram_heap, ptr);
        break;
    case SRAMEX:
        k_heap_free(&sdram_heap, ptr);
        break;
    case SRAM12:
        k_heap_free(&sram12_heap, ptr);
        break;
    case SRAM4:
        k_heap_free(&sram4_heap, ptr);
        break;
    case SRAMDTCM:
        k_heap_free(&dtcm_heap, ptr);
        break;
    case SRAMITCM:
        k_heap_free(&itcm_heap, ptr);
        break;
    default:
        break;
    }

    ptr = NULL;
}

void *myrealloc(uint8_t memx, void *ptr, uint32_t size)
{
    void *new_ptr = NULL;

    switch (memx)
    {
    case SRAMIN:
        new_ptr = k_heap_realloc(&sram_heap, ptr, size, K_NO_WAIT);
        break;
    case SRAMEX:
        new_ptr = k_heap_realloc(&sdram_heap, ptr, size, K_NO_WAIT);
        break;
    case SRAM12:
        new_ptr = k_heap_realloc(&sram12_heap, ptr, size, K_NO_WAIT);
        break;
    case SRAM4:
        new_ptr = k_heap_realloc(&sram4_heap, ptr, size, K_NO_WAIT);
        break;
    case SRAMDTCM:
        new_ptr = k_heap_realloc(&dtcm_heap, ptr, size, K_NO_WAIT);
        break;
    case SRAMITCM:
        new_ptr = k_heap_realloc(&itcm_heap, ptr, size, K_NO_WAIT);
        break;
    default:
        break;
    }

    if (new_ptr == NULL)
    {
        printk("realloc failed for memx=%u, size=%u\n", memx, size);
    }

    return new_ptr;
}
