#include "gt1151q.h"

#include <string.h>
#include <zephyr/sys/util.h>

GT1151Q_Data_t Touch_dev;

/* ©¤©¤ Òý½Å¶¨Òå (PB12=SCL, PB13=SDA, PB14=RST, PH7=INT) ©¤©¤©¤©¤©¤©¤©¤ */
#define I2C_SCL_PORT gpioc
#define I2C_SCL_PIN 6U
#define I2C_SDA_PORT gpioc
#define I2C_SDA_PIN 7U
#define RST_PORT gpiod
#define RST_PIN 13U
#define INT_PORT gpioh
#define INT_PIN 7U

// #define I2C_SCL_PORT gpiob
// #define I2C_SCL_PIN 12U
// #define I2C_SDA_PORT gpiob
// #define I2C_SDA_PIN 13U
// #define RST_PORT gpiob
// #define RST_PIN 14U
// #define INT_PORT gpioh
// #define INT_PIN 7U
/* ©¤©¤ I2C µØÖ· (7-bit ×óÒÆ1Î», Óë HAL °æ±¾Ò»ÖÂ) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */
#define GT1151Q_I2C_ADDR_5D (0x5DU << 1)
#define GT1151Q_I2C_ADDR_14 (0x14U << 1)

/* ©¤©¤ ¼Ä´æÆ÷µØÖ· (16-bit) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */
#define GT1151Q_REG_COMMAND 0x8040U
#define GT1151Q_REG_PRODUCT_ID 0x8140U
#define GT1151Q_REG_STATUS 0x814EU
#define GT1151Q_REG_POINTS 0x8150U

#define GT1151Q_STATUS_DATA_READY_MASK 0x80U
#define GT1151Q_STATUS_COUNT_MASK 0x0FU

#define GT1151Q_POINT_BYTES 8U
#define GT1151Q_MAX_RETRY 2U
#define GT1151Q_LONG_PRESS_MS 800U
#define GT1151Q_RELEASE_HOLD_MS 40U

/* ©¤©¤ Èí¼þ I2C ÑÓÊ± (Ô¼ 100kHz) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */
#define I2C_DELAY_US 5U

/* ©¤©¤ ¾²Ì¬±äÁ¿ ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */
static const struct device *s_scl_dev;
static const struct device *s_sda_dev;
static const struct device *s_rst_dev;
static const struct device *s_int_dev;
static uint16_t s_dev_addr;
static int64_t s_press_start_ms;
static uint8_t s_pressed;

/* ©¤©¤ Èí¼þ I2C µ×²ã²Ù×÷ ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */

static void i2c_delay(void)
{
    k_busy_wait(I2C_DELAY_US);
}

static void i2c_scl_set(int level)
{
    gpio_pin_set(s_scl_dev, I2C_SCL_PIN, level);
}

static void i2c_sda_set(int level)
{
    gpio_pin_set(s_sda_dev, I2C_SDA_PIN, level);
}

static int i2c_sda_get(void)
{
    return gpio_pin_get(s_sda_dev, I2C_SDA_PIN);
}

static void i2c_start(void)
{
    i2c_sda_set(1);
    i2c_scl_set(1);
    i2c_delay();
    i2c_sda_set(0);
    i2c_delay();
    i2c_scl_set(0);
}

static void i2c_stop(void)
{
    i2c_sda_set(0);
    i2c_scl_set(1);
    i2c_delay();
    i2c_sda_set(1);
    i2c_delay();
}

static int i2c_wait_ack(void)
{
    int ack;

    i2c_sda_set(1);
    gpio_pin_configure(s_sda_dev, I2C_SDA_PIN, GPIO_INPUT);
    i2c_delay();
    i2c_scl_set(1);
    i2c_delay();
    ack = i2c_sda_get();
    i2c_scl_set(0);
    gpio_pin_configure(s_sda_dev, I2C_SDA_PIN, GPIO_OUTPUT);
    i2c_delay();
    return ack;
}

static void i2c_send_ack(int ack)
{
    i2c_sda_set(ack ? 1 : 0);
    i2c_delay();
    i2c_scl_set(1);
    i2c_delay();
    i2c_scl_set(0);
    i2c_delay();
}

static void i2c_write_byte(uint8_t data)
{
    for (int i = 7; i >= 0; i--)
    {
        i2c_sda_set((data >> i) & 0x01);
        i2c_delay();
        i2c_scl_set(1);
        i2c_delay();
        i2c_scl_set(0);
    }
}

static uint8_t i2c_read_byte(int ack)
{
    uint8_t data = 0;

    i2c_sda_set(1);
    gpio_pin_configure(s_sda_dev, I2C_SDA_PIN, GPIO_INPUT);

    for (int i = 7; i >= 0; i--)
    {
        i2c_scl_set(1);
        i2c_delay();
        if (i2c_sda_get())
        {
            data |= (1U << i);
        }
        i2c_scl_set(0);
        i2c_delay();
    }

    gpio_pin_configure(s_sda_dev, I2C_SDA_PIN, GPIO_OUTPUT);
    i2c_send_ack(ack);
    return data;
}

/* ©¤©¤ GT1151Q I2C ¶ÁÐ´ (16-bit ¼Ä´æÆ÷µØÖ·) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */

static GT1151Q_Status_e GT1151Q_I2CWrite(uint16_t reg, const uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len == 0U) || (s_dev_addr == 0U))
    {
        return GT1151Q_ERR_PARAM;
    }

    i2c_start();
    i2c_write_byte((uint8_t)(s_dev_addr & 0xFE)); /* Ð´µØÖ· */
    if (i2c_wait_ack())
    {
        i2c_stop();
        return GT1151Q_ERR_BUS;
    }

    i2c_write_byte((uint8_t)(reg >> 8));
    if (i2c_wait_ack())
    {
        i2c_stop();
        return GT1151Q_ERR_BUS;
    }

    i2c_write_byte((uint8_t)(reg & 0xFF));
    if (i2c_wait_ack())
    {
        i2c_stop();
        return GT1151Q_ERR_BUS;
    }

    for (uint16_t i = 0U; i < len; i++)
    {
        i2c_write_byte(buf[i]);
        if (i2c_wait_ack())
        {
            i2c_stop();
            return GT1151Q_ERR_BUS;
        }
    }

    i2c_stop();
    return GT1151Q_OK;
}

static GT1151Q_Status_e GT1151Q_I2CRead(uint16_t reg, uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len == 0U) || (s_dev_addr == 0U))
    {
        return GT1151Q_ERR_PARAM;
    }

    i2c_start();
    i2c_write_byte((uint8_t)(s_dev_addr & 0xFE)); /* Ð´µØÖ· */
    if (i2c_wait_ack())
    {
        i2c_stop();
        return GT1151Q_ERR_BUS;
    }

    i2c_write_byte((uint8_t)(reg >> 8));
    if (i2c_wait_ack())
    {
        i2c_stop();
        return GT1151Q_ERR_BUS;
    }

    i2c_write_byte((uint8_t)(reg & 0xFF));
    if (i2c_wait_ack())
    {
        i2c_stop();
        return GT1151Q_ERR_BUS;
    }

    i2c_start();
    i2c_write_byte((uint8_t)(s_dev_addr | 0x01)); /* ¶ÁµØÖ· */
    if (i2c_wait_ack())
    {
        i2c_stop();
        return GT1151Q_ERR_BUS;
    }

    for (uint16_t i = 0U; i < len; i++)
    {
        buf[i] = i2c_read_byte(i < (len - 1U) ? 0 : 1);
    }

    i2c_stop();
    return GT1151Q_OK;
}

/* ©¤©¤ µØÖ·¼ì²â ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */

static GT1151Q_Status_e GT1151Q_DetectAddress(void)
{
    s_dev_addr = 0U;

    for (uint32_t i = 0U; i <= GT1151Q_MAX_RETRY; i++)
    {
        /* ³¢ÊÔ 0x5D */
        i2c_start();
        i2c_write_byte(GT1151Q_I2C_ADDR_5D & 0xFE);
        if (i2c_wait_ack() == 0)
        {
            i2c_stop();
            s_dev_addr = GT1151Q_I2C_ADDR_5D;
            return GT1151Q_OK;
        }
        i2c_stop();

        /* ³¢ÊÔ 0x14 */
        i2c_start();
        i2c_write_byte(GT1151Q_I2C_ADDR_14 & 0xFE);
        if (i2c_wait_ack() == 0)
        {
            i2c_stop();
            s_dev_addr = GT1151Q_I2C_ADDR_14;
            return GT1151Q_OK;
        }
        i2c_stop();

        k_sleep(K_MSEC(5));
    }

    return GT1151Q_ERR_NOT_READY;
}

/* ©¤©¤ ¸¨Öúº¯Êý ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */

static void GT1151Q_ClearPoints(GT1151Q_Data_t *data)
{
    data->count = 0U;
    memset(data->points, 0, sizeof(data->points));
}

static void GT1151Q_UpdateReleasedState(GT1151Q_Data_t *data)
{
    if (s_pressed == 0U)
    {
        data->state = GT1151Q_TOUCH_IDLE;
        return;
    }

    if ((k_uptime_get() - s_press_start_ms) >= GT1151Q_RELEASE_HOLD_MS)
    {
        s_pressed = 0U;
        data->state = GT1151Q_TOUCH_IDLE;
        GT1151Q_ClearPoints(data);
    }
}

/* ©¤©¤ ¹«¿ª API ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */

GT1151Q_Status_e GT1151Q_Init(void)
{
    s_scl_dev = DEVICE_DT_GET(DT_NODELABEL(I2C_SCL_PORT));
    s_sda_dev = DEVICE_DT_GET(DT_NODELABEL(I2C_SDA_PORT));
    s_rst_dev = DEVICE_DT_GET(DT_NODELABEL(RST_PORT));
    s_int_dev = DEVICE_DT_GET(DT_NODELABEL(INT_PORT));

    if ((s_scl_dev == NULL) || (s_sda_dev == NULL) ||
        (s_rst_dev == NULL) || (s_int_dev == NULL))
    {
        return GT1151Q_ERR_PARAM;
    }

    memset(&Touch_dev, 0, sizeof(Touch_dev));
    s_pressed = 0U;
    s_press_start_ms = 0;

    /* ÅäÖÃ SCL/SDA ÎªÊä³ö, ³õÊ¼¸ß */
    gpio_pin_configure(s_scl_dev, I2C_SCL_PIN, GPIO_OUTPUT);
    gpio_pin_configure(s_sda_dev, I2C_SDA_PIN, GPIO_OUTPUT);
    i2c_scl_set(1);
    i2c_sda_set(1);

    /* ÅäÖÃ RST ºÍ INT ÎªÊä³ö */
    gpio_pin_configure(s_rst_dev, RST_PIN, GPIO_OUTPUT);
    gpio_pin_configure(s_int_dev, INT_PIN, GPIO_OUTPUT);

    /* ©¤©¤ Goodix ÉÏµçÎÕÊÖÊ±Ðò ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ */
    gpio_pin_set(s_rst_dev, RST_PIN, 0);
    gpio_pin_set(s_int_dev, INT_PIN, 0);
    k_sleep(K_MSEC(12));

    gpio_pin_set(s_rst_dev, RST_PIN, 1);
    k_sleep(K_MSEC(8));

    /* INT ÇÐÎªÊäÈë */
    gpio_pin_configure(s_int_dev, INT_PIN, GPIO_INPUT);
    k_sleep(K_MSEC(50));

    /* ¼ì²â I2C µØÖ· */
    GT1151Q_Status_e st = GT1151Q_DetectAddress();
    if (st != GT1151Q_OK)
    {
        return st;
    }

    /* Çå³ý×´Ì¬¼Ä´æÆ÷ */
    uint8_t clear = 0U;
    st = GT1151Q_I2CWrite(GT1151Q_REG_STATUS, &clear, 1U);
    if (st != GT1151Q_OK)
    {
        return st;
    }

    /* Çå³ýÃüÁî¼Ä´æÆ÷ */
    clear = 0U;
    st = GT1151Q_I2CWrite(GT1151Q_REG_COMMAND, &clear, 1U);
    if (st != GT1151Q_OK)
    {
        return st;
    }

    return GT1151Q_OK;
}

GT1151Q_Status_e GT1151Q_ReadProductID(char product_id[5])
{
    if (product_id == NULL)
    {
        return GT1151Q_ERR_PARAM;
    }

    if (GT1151Q_IsReady() == 0U)
    {
        GT1151Q_Status_e st = GT1151Q_DetectAddress();
        if (st != GT1151Q_OK)
        {
            return st;
        }
    }

    uint8_t id_raw[4];
    GT1151Q_Status_e st = GT1151Q_I2CRead(GT1151Q_REG_PRODUCT_ID, id_raw, sizeof(id_raw));
    if (st != GT1151Q_OK)
    {
        return st;
    }

    product_id[0] = (char)id_raw[0];
    product_id[1] = (char)id_raw[1];
    product_id[2] = (char)id_raw[2];
    product_id[3] = (char)id_raw[3];
    product_id[4] = '\0';

    return GT1151Q_OK;
}

GT1151Q_Status_e GT1151Q_ReadData(GT1151Q_Data_t *data)
{
    if (data == NULL)
    {
        return GT1151Q_ERR_PARAM;
    }

    if (GT1151Q_IsReady() == 0U)
    {
        GT1151Q_Status_e st = GT1151Q_DetectAddress();
        if (st != GT1151Q_OK)
        {
            return st;
        }
    }

    /* ¶Á×´Ì¬¼Ä´æÆ÷ */
    uint8_t status;
    GT1151Q_Status_e st = GT1151Q_I2CRead(GT1151Q_REG_STATUS, &status, 1U);
    if (st != GT1151Q_OK)
    {
        return st;
    }

    if ((status & GT1151Q_STATUS_DATA_READY_MASK) == 0U)
    {
        GT1151Q_UpdateReleasedState(data);
        return GT1151Q_ERR_NOT_READY;
    }

    uint8_t count = status & GT1151Q_STATUS_COUNT_MASK;
    if (count > GT1151Q_MAX_TOUCH_POINTS)
    {
        count = GT1151Q_MAX_TOUCH_POINTS;
    }

    GT1151Q_ClearPoints(data);

    if (count > 0U)
    {
        uint8_t point_buf[GT1151Q_MAX_TOUCH_POINTS * GT1151Q_POINT_BYTES];
        st = GT1151Q_I2CRead(GT1151Q_REG_POINTS, point_buf,
                             (uint16_t)(count * GT1151Q_POINT_BYTES));
        if (st != GT1151Q_OK)
        {
            return st;
        }

        data->count = count;
        for (uint8_t i = 0U; i < count; i++)
        {
            uint8_t *p = &point_buf[i * GT1151Q_POINT_BYTES];

            data->points[i].x = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
            data->points[i].y = (uint16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8U));
            data->points[i].size = p[4];
            data->points[i].id = p[7] & 0x0FU;
        }

        if (s_pressed == 0U)
        {
            s_pressed = 1U;
            s_press_start_ms = k_uptime_get();
            data->state = GT1151Q_TOUCH_DOWN;
        }
        else if ((k_uptime_get() - s_press_start_ms) >= GT1151Q_LONG_PRESS_MS)
        {
            data->state = GT1151Q_TOUCH_LONG_PRESS;
        }
        else
        {
            data->state = GT1151Q_TOUCH_DOWN;
        }
    }
    else
    {
        GT1151Q_UpdateReleasedState(data);
    }

    /* Çå³ý×´Ì¬¼Ä´æÆ÷ */
    uint8_t clear = 0U;
    st = GT1151Q_I2CWrite(GT1151Q_REG_STATUS, &clear, 1U);
    if (st != GT1151Q_OK)
    {
        return st;
    }

    return GT1151Q_OK;
}

void GT1151Q_TransformToScreen(GT1151Q_Data_t *data)
{
    if (data == NULL || data->count == 0U)
    {
        return;
    }

    for (uint8_t i = 0U; i < data->count; i++)
    {
        uint16_t x = data->points[i].x;
        uint16_t y = data->points[i].y;

#if GT1151Q_COORD_SWAP_XY
        uint16_t tmp = x;
        x = y;
        y = tmp;
#endif

#if GT1151Q_COORD_MIRROR_X
        x = GT1151Q_SCREEN_WIDTH - 1U - x;
#endif

#if GT1151Q_COORD_MIRROR_Y
        y = GT1151Q_SCREEN_HEIGHT - 1U - y;
#endif

        data->points[i].x = x;
        data->points[i].y = y;
    }
}

uint8_t GT1151Q_IsReady(void)
{
    return (s_dev_addr != 0U) ? 1U : 0U;
}
