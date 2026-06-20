#ifndef __LCD_H__
#define __LCD_H__

#include <stdint.h>

/* RGB565 ÑÕÉ«ºê */
#define RGB565(r, g, b) ((((uint16_t)(r) & 0xF8) << 8) | \
                         (((uint16_t)(g) & 0xFC) << 3) | \
                         (((uint16_t)(b) & 0xF8) >> 3))
#define LCD_W 800U
#define LCD_H 480U
#define LCD_PIXSIZE 2U /* RGB565 Ã¿ÏñËØ2×Ö½Ú */

#endif /* __LCD_H__ */
