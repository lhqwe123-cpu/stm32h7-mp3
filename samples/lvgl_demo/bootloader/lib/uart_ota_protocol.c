/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file uart_ota_protocol.c
 * @brief 串口 OTA 协议实现 (兼容 YModem)
 *
 * 保留原有自定义协议函数以保持向后兼容。
 * 新增 YModem 标准 CRC-16/CCITT 计算函数。
 */

#include "uart_ota_protocol.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

#include <string.h>

LOG_MODULE_REGISTER(uota_proto, LOG_LEVEL_INF);

/* ============================================================
 * CRC-16/MODBUS 计算 (原有自定义协议)
 * ============================================================ */

uint16_t uota_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }

    return crc;
}

/* ============================================================
 * CRC-16/CCITT 计算 (YModem 标准)
 * ============================================================ */

uint16_t uota_crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

/* ============================================================
 * 包构建
 * ============================================================ */

int uota_build_packet(uint8_t *buf, size_t size,
                      uint16_t seq, uota_pkt_type_t type,
                      const uint8_t *data, uint16_t len)
{
    size_t total = UOTA_HDR_SIZE + len + 2 + UOTA_TAIL_SIZE;

    if (size < total)
    {
        return -1;
    }

    /* 同步头 */
    buf[0] = UOTA_SYNC1;
    buf[1] = UOTA_SYNC2;

    /* 序号 (大端) */
    buf[2] = (seq >> 8) & 0xFF;
    buf[3] = seq & 0xFF;

    /* 类型 */
    buf[4] = (uint8_t)type;

    /* 长度 (大端) */
    buf[5] = (len >> 8) & 0xFF;
    buf[6] = len & 0xFF;

    /* 数据 */
    if (data && len > 0)
    {
        memcpy(&buf[7], data, len);
    }

    /* CRC16 覆盖 SEQ+TYPE+LEN+DATA */
    uint16_t crc = uota_crc16(&buf[2], 5 + len);
    buf[7 + len] = crc & 0xFF;
    buf[8 + len] = (crc >> 8) & 0xFF;

    /* 包尾 */
    buf[9 + len] = UOTA_END1;
    buf[10 + len] = UOTA_END2;

    return (int)total;
}

int uota_build_ack(uint8_t *buf, size_t size,
                   uint16_t seq, uota_ack_t ack_code)
{
    uint8_t ack_data[1];

    ack_data[0] = (uint8_t)ack_code;

    if (ack_code == UOTA_ACK_OK)
    {
        return uota_build_packet(buf, size, seq, UOTA_TYPE_ACK,
                                 ack_data, 1);
    }
    else
    {
        return uota_build_packet(buf, size, seq, UOTA_TYPE_NACK,
                                 ack_data, 1);
    }
}

int uota_build_cmd(uint8_t *buf, size_t size,
                   uint16_t seq, uota_cmd_t cmd,
                   const uint8_t *data, uint16_t len)
{
    size_t total_len = len + 1; /* cmd byte + data */
    uint8_t *cmd_buf;
    int ret;

    if (total_len > UOTA_MAX_PAYLOAD_SIZE)
    {
        return -1;
    }

    cmd_buf = (uint8_t *)k_malloc(total_len);
    if (!cmd_buf)
    {
        return -1;
    }

    cmd_buf[0] = (uint8_t)cmd;
    if (data && len > 0)
    {
        memcpy(&cmd_buf[1], data, len);
    }

    ret = uota_build_packet(buf, size, seq, UOTA_TYPE_CMD,
                            cmd_buf, (uint16_t)total_len);
    k_free(cmd_buf);

    return ret;
}

/* ============================================================
 * 包解析
 * ============================================================ */

int uota_parse_packet(const uint8_t *buf, size_t size,
                      uota_packet_t *pkt)
{
    uint16_t len;
    uint16_t crc_received, crc_calc;
    size_t expected_size;

    if (!buf || !pkt || size < UOTA_HDR_SIZE + UOTA_TAIL_SIZE)
    {
        return -1;
    }

    /* 验证同步头 */
    if (buf[0] != UOTA_SYNC1 || buf[1] != UOTA_SYNC2)
    {
        return -2;
    }

    /* 解析字段 */
    pkt->sync1 = buf[0];
    pkt->sync2 = buf[1];
    pkt->seq = ((uint16_t)buf[2] << 8) | buf[3];
    pkt->type = buf[4];
    pkt->len = ((uint16_t)buf[5] << 8) | buf[6];

    len = pkt->len;

    /* 验证类型 */
    if (pkt->type < UOTA_TYPE_CMD || pkt->type > UOTA_TYPE_EOT)
    {
        return -3;
    }

    /* 验证长度 */
    if (len > UOTA_MAX_PAYLOAD_SIZE)
    {
        return -4;
    }

    expected_size = UOTA_HDR_SIZE + len + 2 + UOTA_TAIL_SIZE;
    if (size < expected_size)
    {
        return -5;
    }

    /* 验证包尾 */
    if (buf[7 + len + 2] != UOTA_END1 ||
        buf[7 + len + 3] != UOTA_END2)
    {
        return -6;
    }

    /* 验证 CRC */
    crc_received = buf[7 + len] | ((uint16_t)buf[8 + len] << 8);
    crc_calc = uota_crc16(&buf[2], 5 + len);

    if (crc_received != crc_calc)
    {
        LOG_DBG("CRC mismatch: recv=0x%04X calc=0x%04X",
                crc_received, crc_calc);
        return -7;
    }

    /* 设置数据指针 (指向缓冲区内部) */
    if (len > 0)
    {
        pkt->data = (uint8_t *)&buf[7];
    }
    else
    {
        pkt->data = NULL;
    }

    pkt->crc = crc_received;
    pkt->end1 = buf[7 + len + 2];
    pkt->end2 = buf[7 + len + 3];

    return 0;
}

/* ============================================================
 * 字符串转换
 * ============================================================ */

const char *uota_type_str(uota_pkt_type_t type)
{
    switch (type)
    {
    case UOTA_TYPE_CMD:
        return "CMD";
    case UOTA_TYPE_DATA:
        return "DATA";
    case UOTA_TYPE_ACK:
        return "ACK";
    case UOTA_TYPE_NACK:
        return "NACK";
    case UOTA_TYPE_EOT:
        return "EOT";
    default:
        return "UNKNOWN";
    }
}

const char *uota_cmd_str(uota_cmd_t cmd)
{
    switch (cmd)
    {
    case UOTA_CMD_START:
        return "START";
    case UOTA_CMD_DATA:
        return "DATA";
    case UOTA_CMD_VERIFY:
        return "VERIFY";
    case UOTA_CMD_ABORT:
        return "ABORT";
    case UOTA_CMD_COMPLETE:
        return "COMPLETE";
    default:
        return "UNKNOWN";
    }
}

const char *uota_ack_str(uota_ack_t ack)
{
    switch (ack)
    {
    case UOTA_ACK_OK:
        return "OK";
    case UOTA_ACK_CRC_ERR:
        return "CRC_ERR";
    case UOTA_ACK_SEQ_ERR:
        return "SEQ_ERR";
    case UOTA_ACK_SIZE_ERR:
        return "SIZE_ERR";
    case UOTA_ACK_FLASH_ERR:
        return "FLASH_ERR";
    case UOTA_ACK_BUSY:
        return "BUSY";
    case UOTA_ACK_ABORT:
        return "ABORT";
    default:
        return "UNKNOWN";
    }
}

const char *uota_state_str(uota_state_t state)
{
    switch (state)
    {
    case UOTA_STATE_IDLE:
        return "IDLE";
    case UOTA_STATE_WAIT_SYNC:
        return "WAIT_SYNC";
    case UOTA_STATE_RECEIVING:
        return "RECEIVING";
    case UOTA_STATE_VERIFYING:
        return "VERIFYING";
    case UOTA_STATE_COMPLETE:
        return "COMPLETE";
    case UOTA_STATE_FAILED:
        return "FAILED";
    case UOTA_STATE_ABORTED:
        return "ABORTED";
    default:
        return "UNKNOWN";
    }
}
