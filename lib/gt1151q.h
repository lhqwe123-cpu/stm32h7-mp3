#ifndef __GT1151Q_H
#define __GT1151Q_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define GT1151Q_MAX_TOUCH_POINTS 5U

#define GT1151Q_SCREEN_WIDTH 800U
#define GT1151Q_SCREEN_HEIGHT 480U
#define GT1151Q_COORD_SWAP_XY 0U
#define GT1151Q_COORD_MIRROR_X 0U
#define GT1151Q_COORD_MIRROR_Y 0U

    typedef enum GT1151Q_Status
    {
        GT1151Q_OK = 0,
        GT1151Q_ERR_PARAM,
        GT1151Q_ERR_NOT_READY,
        GT1151Q_ERR_BUS,
        GT1151Q_ERR_TIMEOUT
    } GT1151Q_Status_e;

    typedef struct GT1151Q_Point
    {
        uint8_t id;
        uint16_t x;
        uint16_t y;
        uint8_t size;
    } GT1151Q_Point_t;

    typedef enum GT1151Q_TouchState
    {
        GT1151Q_TOUCH_IDLE = 0,
        GT1151Q_TOUCH_DOWN,
        GT1151Q_TOUCH_LONG_PRESS,
    } GT1151Q_TouchState_e;

    typedef struct GT1151Q_Data_t
    {
        uint8_t count;
        GT1151Q_TouchState_e state;
        GT1151Q_Point_t points[GT1151Q_MAX_TOUCH_POINTS];
    } GT1151Q_Data_t;

    GT1151Q_Status_e GT1151Q_Init(void);
    GT1151Q_Status_e GT1151Q_ReadData(GT1151Q_Data_t *data);
    GT1151Q_Status_e GT1151Q_ReadProductID(char product_id[5]);
    void GT1151Q_TransformToScreen(GT1151Q_Data_t *data);
    uint8_t GT1151Q_IsReady(void);

    extern GT1151Q_Data_t Touch_dev;

#ifdef __cplusplus
}
#endif

#endif /* __GT1151Q_H */
