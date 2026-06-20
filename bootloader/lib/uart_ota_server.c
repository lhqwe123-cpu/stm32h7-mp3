/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uart_ota_server.h"
#include "ymodem.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/devicetree.h>
#include <zephyr/cache.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(uota_server, LOG_LEVEL_WRN);

#define UOTA_TX_BUF_SIZE 64
#define UOTA_DMA_BUF_SIZE 256

static const struct device *uota_uart_dev;
static uint8_t __nocache __aligned(32) uota_dma_buf[2][UOTA_DMA_BUF_SIZE];
static int uota_dma_cur_buf;
static uint8_t __nocache __aligned(32) uota_tx_dma_buf[UOTA_TX_BUF_SIZE];
static uint8_t uota_ring_data[UOTA_RING_BUF_SIZE];
static struct ring_buf uota_ring;
static struct k_sem uota_rx_sem;
static struct k_sem uota_tx_sem;
static bool uota_dma_running = true;

static void uota_uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);
    switch (evt->type)
    {
    case UART_RX_RDY:
        ring_buf_put(&uota_ring, evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len);
        k_sem_give(&uota_rx_sem);
        break;
    case UART_RX_BUF_REQUEST:
        uota_dma_cur_buf = (uota_dma_cur_buf + 1) % 2;
        uart_rx_buf_rsp(dev, uota_dma_buf[uota_dma_cur_buf], UOTA_DMA_BUF_SIZE);
        break;
    case UART_RX_DISABLED:
        if (uota_dma_running)
        {
            uota_dma_cur_buf = 0;
            uart_rx_enable(dev, uota_dma_buf[0], UOTA_DMA_BUF_SIZE, 100000);
        }
        break;
    case UART_TX_DONE:
        k_sem_give(&uota_tx_sem);
        break;
    case UART_TX_ABORTED:
        k_sem_give(&uota_tx_sem);
        break;
    default:
        break;
    }
}

static int uota_uart_rx_start(void)
{
    uota_dma_running = true;
    uart_callback_set(uota_uart_dev, uota_uart_cb, NULL);
    uota_dma_cur_buf = 0;
    int r = uart_rx_enable(uota_uart_dev, uota_dma_buf[0], UOTA_DMA_BUF_SIZE, 100000);
    return r;
}

static void uota_uart_rx_stop(void)
{
    uota_dma_running = false;
    uart_rx_disable(uota_uart_dev);
}

static int uota_uart_tx(const uint8_t *data, size_t len)
{
    static bool inited;
    if (!inited)
    {
        k_sem_init(&uota_tx_sem, 0, 1);
        inited = true;
    }
    if (len > UOTA_TX_BUF_SIZE)
        len = UOTA_TX_BUF_SIZE;
    memcpy(uota_tx_dma_buf, data, len);
    int ret = uart_tx(uota_uart_dev, uota_tx_dma_buf, len, SYS_FOREVER_US);
    if (ret)
        return ret;
    if (k_sem_take(&uota_tx_sem, K_MSEC(2000)))
        return -ETIMEDOUT;
    return 0;
}

static int uota_putc(uint8_t c) { return uota_uart_tx(&c, 1); }

static int uota_getc(uint8_t *c, uint32_t timeout_ms)
{
    uint32_t start = k_uptime_get_32();
    while (uota_dma_running)
    {
        if (ring_buf_get(&uota_ring, c, 1) == 1)
            return 0;
        if (k_sem_take(&uota_rx_sem, K_MSEC(50)) != 0)
        {
            if ((k_uptime_get_32() - start) > timeout_ms)
                return -ETIMEDOUT;
        }
    }
    return -ECANCELED;
}

static void uota_flush_rx(void)
{
    uint8_t d;
    while (ring_buf_get(&uota_ring, &d, 1) == 1)
        ;
}

static int uota_ymodem_recv_packet(uint8_t *buf, size_t buf_size, size_t *pkt_size, uint8_t *seq, uint32_t timeout_ms)
{
    uint8_t header, seq_byte, seq_comp;
    int ret;
    size_t data_size;

    /* Search for valid header (SOH/STX/EOT/CAN), skip invalid bytes */
    while (1)
    {
        ret = uota_getc(&header, timeout_ms);
        if (ret)
            return -ETIMEDOUT;

        switch (header)
        {
        case YMODEM_SOH:
            data_size = 128;
            goto found;
        case YMODEM_STX:
            data_size = 1024;
            goto found;
        case YMODEM_EOT:
            return 1;
        case YMODEM_CAN:
            uota_getc(&header, 100);
            return -ECANCELED;
        default:
            continue; /* skip invalid byte */
        }
    }
found:

    ret = uota_getc(&seq_byte, timeout_ms);
    if (ret)
        return -ETIMEDOUT;
    ret = uota_getc(&seq_comp, timeout_ms);
    if (ret)
        return -ETIMEDOUT;
    if ((uint8_t)(seq_byte + seq_comp) != 0xFF)
        return -EINVAL;

    if (data_size > buf_size)
        return -ENOMEM;

    size_t remaining = data_size;
    uint32_t data_deadline = k_uptime_get_32() + timeout_ms;
    while (remaining > 0)
    {
        uint32_t now = k_uptime_get_32();
        if ((now - data_deadline) < 0x80000000)
            return -ETIMEDOUT;
        uint32_t n = ring_buf_get(&uota_ring, buf + (data_size - remaining), remaining);
        if (n > 0)
        {
            remaining -= n;
            continue;
        }
        uint32_t w = data_deadline - now;
        if (w > 100)
            w = 100;
        k_sem_take(&uota_rx_sem, K_MSEC(w));
    }

    uint8_t crc_h, crc_l;
    ret = uota_getc(&crc_h, timeout_ms);
    if (ret)
        return -ETIMEDOUT;
    ret = uota_getc(&crc_l, timeout_ms);
    if (ret)
        return -ETIMEDOUT;
    uint16_t crc_recv = ((uint16_t)crc_h << 8) | crc_l;
    if (crc_recv != uota_crc16_ccitt(buf, data_size))
        return -EBADMSG;

    *pkt_size = data_size;
    *seq = seq_byte;
    return 0;
}

static int uota_parse_filename_packet(const uint8_t *data, size_t size, uota_start_info_t *info)
{
    if (data[0] == '\0')
        return 0;
    const char *p = (const char *)data;
    size_t nl = strnlen(p, size);
    const char *sz = p + nl + 1;
    if ((size_t)(sz - (const char *)data) < size)
        info->image_size = (uint32_t)strtoul(sz, NULL, 10);
    LOG_INF("YModem file: %.*s, size=%u", (int)nl, p, info->image_size);
    return 1;
}

static int uota_flash_erase_slot1(void)
{
    const struct flash_area *fa;
    int r = flash_area_open(PARTITION_ID(slot1_partition), &fa);
    if (r)
        return r;
    LOG_INF("Erasing slot1...");
    r = flash_area_erase(fa, 0, fa->fa_size);
    flash_area_close(fa);
    if (r == 0)
        LOG_INF("Slot1 erased");
    return r;
}

static int uota_flash_write(uint32_t off, const uint8_t *d, size_t len)
{
    const struct flash_area *fa;
    int r = flash_area_open(PARTITION_ID(slot1_partition), &fa);
    if (r)
        return r;
    r = flash_area_write(fa, off, d, len);
    flash_area_close(fa);
    return r;
}

static int uota_request_upgrade(void) { return boot_request_upgrade(BOOT_UPGRADE_TEST); }

#define UOTA_CHANGE_STATE(srv, s)                                          \
    do                                                                     \
    {                                                                      \
        uota_state_t _o = (srv)->state;                                    \
        (srv)->state = (s);                                                \
        LOG_INF("State: %s -> %s", uota_state_str(_o), uota_state_str(s)); \
        if ((srv)->state_cb)                                               \
            (srv)->state_cb((s), (srv)->user_data);                        \
    } while (0)

#define UOTA_NOTIFY_PROGRESS(srv)                                                                 \
    do                                                                                            \
    {                                                                                             \
        if ((srv)->progress_cb && (srv)->total_bytes > 0)                                         \
        {                                                                                         \
            uint8_t _p = (uint8_t)(((uint64_t)(srv)->bytes_received * 100) / (srv)->total_bytes); \
            (srv)->progress_cb(_p, (srv)->user_data);                                             \
        }                                                                                         \
    } while (0)

#define UOTA_NOTIFY_ERROR(srv, c, m)                     \
    do                                                   \
    {                                                    \
        if ((srv)->error_cb)                             \
            (srv)->error_cb((c), (m), (srv)->user_data); \
    } while (0)

int uota_server_init(uota_server_t *s, const char *dev, uint32_t baud)
{
    if (!s)
        return -EINVAL;
    memset(s, 0, sizeof(*s));
    s->uart_dev_name = dev ? dev : UOTA_DEFAULT_UART_DEV;
    s->baudrate = baud ? baud : UOTA_DEFAULT_BAUDRATE;
    s->state = UOTA_STATE_IDLE;
    s->initialized = true;
    LOG_INF("UOTA server init: dev=%s baud=%u", s->uart_dev_name, s->baudrate);
    return 0;
}

int uota_server_start(uota_server_t *s)
{
    uint8_t pkt_buf[UOTA_MAX_PKT_SIZE];
    size_t pkt_size;
    uint8_t seq;
    uint32_t flash_offset = 0;
    int ret, retry;

    if (!s || !s->initialized)
        return -EINVAL;

    uota_uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3));
    if (!device_is_ready(uota_uart_dev))
        uota_uart_dev = device_get_binding(s->uart_dev_name);
    if (!uota_uart_dev)
    {
        LOG_ERR("UART not found");
        return -ENODEV;
    }

    LOG_INF("Starting on %s @ %u", s->uart_dev_name, s->baudrate);

    ring_buf_init(&uota_ring, sizeof(uota_ring_data), uota_ring_data);
    k_sem_init(&uota_rx_sem, 0, 1);

    s->state = UOTA_STATE_IDLE;
    s->transfer_active = false;
    s->bytes_received = 0;
    s->total_bytes = 0;
    s->expected_seq = 0;
    memset(&s->stats, 0, sizeof(s->stats));
    memset(&s->fw_info, 0, sizeof(s->fw_info));

    ret = uota_uart_rx_start();
    if (ret)
    {
        LOG_ERR("RX start failed: %d", ret);
        return ret;
    }

    /* Phase 1: Handshake */
    UOTA_CHANGE_STATE(s, UOTA_STATE_WAIT_SYNC);
    uota_flush_rx();
    retry = 0;
    while (retry < UOTA_MAX_RETRIES && !s->transfer_active)
    {
        if (!uota_dma_running)
        {
            UOTA_CHANGE_STATE(s, UOTA_STATE_ABORTED);
            goto exit;
        }
        ret = uota_putc(YMODEM_C);
        if (ret)
        {
            retry++;
            k_sleep(K_MSEC(500));
            continue;
        }
        LOG_INF("Sent 'C' (attempt %d)", retry + 1);

        ret = uota_ymodem_recv_packet(pkt_buf, sizeof(pkt_buf), &pkt_size, &seq, 3000);
        if (ret == 0 && seq == 0)
        {
            int pr = uota_parse_filename_packet(pkt_buf, pkt_size, &s->fw_info);
            if (pr > 0)
            {
                s->total_bytes = s->fw_info.image_size;

                /* Erase flash before starting data transfer. */
                uota_uart_rx_stop();
                uota_flush_rx();

                ret = uota_flash_erase_slot1();
                if (ret)
                {
                    UOTA_NOTIFY_ERROR(s, UOTA_ACK_FLASH_ERR, "Erase failed");
                    UOTA_CHANGE_STATE(s, UOTA_STATE_FAILED);
                    goto exit;
                }

                /* Restart RX first, then send ACK+C.
                 * Zephyr UART is full-duplex, RX and TX can coexist. */
                ret = uota_uart_rx_start();
                if (ret)
                {
                    LOG_ERR("RX restart failed: %d", ret);
                    UOTA_CHANGE_STATE(s, UOTA_STATE_FAILED);
                    goto exit;
                }

                uota_putc(YMODEM_ACK);
                uota_putc(YMODEM_C);

                s->expected_seq = 1;
                s->transfer_active = true;
                UOTA_CHANGE_STATE(s, UOTA_STATE_RECEIVING);
                break;
            }
            else if (pr == 0)
            {
                uota_putc(YMODEM_ACK);
                UOTA_CHANGE_STATE(s, UOTA_STATE_COMPLETE);
                goto exit;
            }
        }
        else if (ret == -ETIMEDOUT)
        {
            retry++;
        }
        else
        {
            uota_putc(YMODEM_NAK);
            retry++;
        }
    }
    if (retry >= UOTA_MAX_RETRIES)
    {
        UOTA_NOTIFY_ERROR(s, UOTA_ACK_BUSY, "Handshake timeout");
        UOTA_CHANGE_STATE(s, UOTA_STATE_FAILED);
        goto exit;
    }

    /* Phase 2: Receive data */
    LOG_INF("Receiving data...");
    retry = 0;
    while (s->transfer_active)
    {
        ret = uota_ymodem_recv_packet(pkt_buf, sizeof(pkt_buf), &pkt_size, &seq, YMODEM_PACKET_TIMEOUT_MS);
        if (ret == 0)
        {
            if (seq == s->expected_seq)
            {
                size_t wlen = pkt_size;
                if (s->bytes_received + pkt_size > s->total_bytes)
                    wlen = s->total_bytes - s->bytes_received;

                /* Skip .fwpkg header (first 64 bytes) */
                const uint8_t *write_ptr = pkt_buf;
                if (flash_offset == 0)
                {
                    write_ptr += 64;
                    wlen -= 64;
                }

                if (wlen > 0)
                {
                    ret = uota_flash_write(flash_offset, write_ptr, wlen);
                    if (ret)
                    {
                        uota_putc(YMODEM_CAN);
                        uota_putc(YMODEM_CAN);
                        UOTA_NOTIFY_ERROR(s, UOTA_ACK_FLASH_ERR, "Flash write failed");
                        UOTA_CHANGE_STATE(s, UOTA_STATE_FAILED);
                        goto exit;
                    }
                    flash_offset += wlen;
                }
                s->bytes_received += pkt_size;
                s->expected_seq++;
                if (s->expected_seq == 0)
                    s->expected_seq = 1;
                retry = 0;
                uota_putc(YMODEM_ACK);
                UOTA_NOTIFY_PROGRESS(s);
                LOG_INF("Pkt seq=%u size=%zu total=%u/%u", seq, wlen, s->bytes_received, s->total_bytes);
                if (s->bytes_received >= s->total_bytes)
                {
                    /* All data received. Wait for EOT from PC. */
                    LOG_INF("All data received, waiting for EOT...");
                    ret = uota_ymodem_recv_packet(pkt_buf, sizeof(pkt_buf), &pkt_size, &seq, 10000);
                    if (ret == 1)
                    {
                        /* EOT received - standard YModem end sequence */
                        uota_putc(YMODEM_NAK);
                        ret = uota_ymodem_recv_packet(pkt_buf, sizeof(pkt_buf), &pkt_size, &seq, 3000);
                        if (ret == 1)
                        {
                            uota_putc(YMODEM_ACK);
                            uota_putc(YMODEM_C);
                            ret = uota_ymodem_recv_packet(pkt_buf, sizeof(pkt_buf), &pkt_size, &seq, 3000);
                            if (ret == 0 && seq == 0)
                                uota_putc(YMODEM_ACK);
                        }
                    }
                    ret = uota_request_upgrade();
                    UOTA_CHANGE_STATE(s, ret ? UOTA_STATE_FAILED : UOTA_STATE_COMPLETE);
                    goto exit;
                }
            }
            else if (seq == (uint8_t)(s->expected_seq - 1))
            {
                uota_putc(YMODEM_ACK);
            }
            else
            {
                LOG_WRN("Seq mismatch: expected=%u got=%u", s->expected_seq, seq);
                uota_putc(YMODEM_NAK);
                retry++;
            }
        }
        else if (ret == 1)
        {
            /* EOT */
            uota_putc(YMODEM_NAK);
            k_sleep(K_MSEC(100));
            uota_getc(&seq, 2000);
            uota_putc(YMODEM_ACK);
            uota_putc(YMODEM_C);
            ret = uota_ymodem_recv_packet(pkt_buf, sizeof(pkt_buf), &pkt_size, &seq, 3000);
            if (ret == 0 && seq == 0)
                uota_putc(YMODEM_ACK);
            ret = uota_request_upgrade();
            UOTA_CHANGE_STATE(s, ret ? UOTA_STATE_FAILED : UOTA_STATE_COMPLETE);
            goto exit;
        }
        else if (ret == -ETIMEDOUT)
        {
            retry++;
            s->stats.timeouts++;
            if (retry >= UOTA_MAX_RETRIES)
            {
                UOTA_NOTIFY_ERROR(s, UOTA_ACK_BUSY, "Timeout");
                UOTA_CHANGE_STATE(s, UOTA_STATE_FAILED);
                goto exit;
            }
            uota_putc(YMODEM_NAK);
        }
        else if (ret == -ECANCELED)
        {
            UOTA_CHANGE_STATE(s, UOTA_STATE_ABORTED);
            goto exit;
        }
        else
        {
            retry++;
            s->stats.crc_errors++;
            if (retry >= UOTA_MAX_RETRIES)
            {
                UOTA_NOTIFY_ERROR(s, UOTA_ACK_CRC_ERR, "Too many errors");
                UOTA_CHANGE_STATE(s, UOTA_STATE_FAILED);
                goto exit;
            }
            uota_putc(YMODEM_NAK);
        }
    }

exit:
    uota_uart_rx_stop();
    LOG_INF("Stopped: %s bytes=%u/%u", uota_state_str(s->state), s->bytes_received, s->total_bytes);
    return (s->state == UOTA_STATE_COMPLETE) ? 0 : -1;
}

int uota_server_abort(uota_server_t *s)
{
    if (!s)
        return -EINVAL;
    s->transfer_active = false;
    uota_dma_running = false;
    k_sem_give(&uota_rx_sem);
    UOTA_CHANGE_STATE(s, UOTA_STATE_ABORTED);
    return 0;
}

int uota_server_get_stats(const uota_server_t *s, uota_stats_t *st)
{
    if (!s || !st)
        return -EINVAL;
    memcpy(st, &s->stats, sizeof(*st));
    return 0;
}
uota_state_t uota_server_get_state(const uota_server_t *s) { return s ? s->state : UOTA_STATE_IDLE; }
uint8_t uota_server_get_progress(const uota_server_t *s)
{
    if (!s || s->total_bytes == 0)
        return 0;
    return (uint8_t)(((uint64_t)s->bytes_received * 100) / s->total_bytes);
}
void uota_server_set_state_cb(uota_server_t *s, uota_state_cb_t cb)
{
    if (s)
        s->state_cb = cb;
}
void uota_server_set_progress_cb(uota_server_t *s, uota_progress_cb_t cb)
{
    if (s)
        s->progress_cb = cb;
}
void uota_server_set_error_cb(uota_server_t *s, uota_error_cb_t cb)
{
    if (s)
        s->error_cb = cb;
}
