#ifndef __MY_MALLOC_H__
#define __MY_MALLOC_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SRAM_HEAP_BASE 0x24000000UL
#define SRAM_HEAP_SIZE (25U * 1024U)

#define SDRAM_HEAP_BASE 0XC1000000UL
#define SDRAM_HEAP_SIZE (16U * 1024U * 1024U)

#define SRAM12_HEAP_BASE 0x30000000UL
#define SRAM12_HEAP_SIZE (240U * 1024U)

#define SRAM3_HEAP_BASE 0x38000000UL
#define SRAM3_HEAP_SIZE (60U * 1024U)

/* ── 四个内存池定义 ──────────────────────────────────────────── */
#define SRAMIN 0
#define SRAMEX 1
#define SRAM12 2
#define SRAM4 3
#define SRAMBANK 6

#ifndef NULL
#define NULL 0
#endif

    void my_heap_init(void);
    void myfree(uint8_t memx, void *ptr);
    void *mymalloc(uint8_t memx, uint32_t size);
    void *myrealloc(uint8_t memx, void *ptr, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* __MY_MALLOC_H__ */
