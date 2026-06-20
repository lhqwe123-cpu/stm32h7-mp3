/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file uart_ota_protocol.h
 * @brief 串口 OTA 协议定义 (兼容 YModem 协议)
 *
 * 本文件保留原有自定义协议定义以保持向后兼容，
 * 实际传输层已切换为 YModem 协议 (参见 ymodem.h)。
 *
 * YModem 协议使用标准控制字符:
 *   SOH=0x01, STX=0x02, EOT=0x04, ACK=0x06, NAK=0x15, CAN=0x18, 'C'=0x43
 */

#ifndef __UART_OTA_PROTOCOL_H__
#define __UART_OTA_PROTOCOL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================
 * 协议常量
 * ============================================================ */

/** 同步头 */
#define UOTA_SYNC1 0xA5
#define UOTA_SYNC2 0x5A

/** 包尾 */
#define UOTA_END1 0x0D
#define UOTA_END2 0x0A

/** 包头大小 (SYNC+SEQ+TYPE+LEN) */
#define UOTA_HDR_SIZE 7

/** 包尾大小 */
#define UOTA_TAIL_SIZE 4

/** 包开销 (包头+CRC+包尾) */
#define UOTA_OVERHEAD (UOTA_HDR_SIZE + 2 + UOTA_TAIL_SIZE)

/** 默认数据载荷大小 */
#define UOTA_DEFAULT_PAYLOAD_SIZE 1024

/** 最大数据载荷大小 */
#define UOTA_MAX_PAYLOAD_SIZE 2048

/** 接收缓冲区大小 */
#define UOTA_RX_BUF_SIZE (UOTA_MAX_PAYLOAD_SIZE + UOTA_OVERHEAD + 64)

/** 最大重传次数 (握手阶段) */
#define UOTA_MAX_RETRIES 60

/** 应答超时 (毫秒) */
#define UOTA_ACK_TIMEOUT_MS 500

/** 同步超时 (毫秒) */
#define UOTA_SYNC_TIMEOUT_MS 60000

    /* ============================================================
     * 包类型
     * ============================================================ */

    typedef enum
    {
        UOTA_TYPE_CMD = 0x01,  /**< 命令包 */
        UOTA_TYPE_DATA = 0x02, /**< 数据包 */
        UOTA_TYPE_ACK = 0x03,  /**< 应答包 */
        UOTA_TYPE_NACK = 0x04, /**< 否定应答包 */
        UOTA_TYPE_EOT = 0x05,  /**< 传输结束 */
    } uota_pkt_type_t;

    /* ============================================================
     * 命令码
     * ============================================================ */

    typedef enum
    {
        UOTA_CMD_START = 0x10,    /**< 开始传输 */
        UOTA_CMD_DATA = 0x20,     /**< 数据传输 */
        UOTA_CMD_VERIFY = 0x30,   /**< 请求校验 */
        UOTA_CMD_ABORT = 0x40,    /**< 中止传输 */
        UOTA_CMD_COMPLETE = 0x50, /**< 传输完成 */
    } uota_cmd_t;

    /* ============================================================
     * 应答码
     * ============================================================ */

    typedef enum
    {
        UOTA_ACK_OK = 0x00,        /**< 成功 */
        UOTA_ACK_CRC_ERR = 0x01,   /**< CRC 校验错误 */
        UOTA_ACK_SEQ_ERR = 0x02,   /**< 序号错误 */
        UOTA_ACK_SIZE_ERR = 0x03,  /**< 大小错误 */
        UOTA_ACK_FLASH_ERR = 0x04, /**< Flash 写入错误 */
        UOTA_ACK_BUSY = 0x05,      /**< 设备忙 */
        UOTA_ACK_ABORT = 0x06,     /**< 中止 */
    } uota_ack_t;

    /* ============================================================
     * 传输状态
     * ============================================================ */

    typedef enum
    {
        UOTA_STATE_IDLE,      /**< 空闲 */
        UOTA_STATE_WAIT_SYNC, /**< 等待同步 */
        UOTA_STATE_RECEIVING, /**< 接收数据中 */
        UOTA_STATE_VERIFYING, /**< 校验中 */
        UOTA_STATE_COMPLETE,  /**< 传输完成 */
        UOTA_STATE_FAILED,    /**< 传输失败 */
        UOTA_STATE_ABORTED,   /**< 已中止 */
    } uota_state_t;

    /* ============================================================
     * 开始传输信息 (CMD_START 携带的数据)
     * ============================================================ */

    typedef struct
    {
        uint32_t magic;         /**< 魔数 FWPKG_MAGIC_VAL */
        uint32_t version;       /**< 固件版本 */
        uint32_t image_size;    /**< 镜像大小 */
        uint8_t image_hash[32]; /**< SHA256 哈希 */
        uint8_t reserved[20];   /**< 保留 */
    } uota_start_info_t;

    /* ============================================================
     * 数据包结构 (内部使用)
     * ============================================================ */

    typedef struct
    {
        uint8_t sync1; /**< 同步头1 */
        uint8_t sync2; /**< 同步头2 */
        uint16_t seq;  /**< 包序号 */
        uint8_t type;  /**< 包类型 */
        uint16_t len;  /**< 数据长度 */
        uint8_t *data; /**< 数据指针 (不拥有) */
        uint16_t crc;  /**< CRC16 */
        uint8_t end1;  /**< 包尾1 */
        uint8_t end2;  /**< 包尾2 */
    } uota_packet_t;

    /* ============================================================
     * 传输统计
     * ============================================================ */

    typedef struct
    {
        uint32_t total_bytes;      /**< 总字节数 */
        uint32_t received_bytes;   /**< 已接收字节数 */
        uint32_t total_packets;    /**< 总包数 */
        uint32_t received_packets; /**< 已接收包数 */
        uint32_t retransmissions;  /**< 重传次数 */
        uint32_t crc_errors;       /**< CRC 错误次数 */
        uint32_t seq_errors;       /**< 序号错误次数 */
        uint32_t timeouts;         /**< 超时次数 */
    } uota_stats_t;

    /* ============================================================
     * 回调类型
     * ============================================================ */

    /**
     * @brief 状态变更回调
     * @param state    新状态
     * @param user_data 用户数据
     */
    typedef void (*uota_state_cb_t)(uota_state_t state, void *user_data);

    /**
     * @brief 进度回调
     * @param progress 进度 (0-100)
     * @param user_data 用户数据
     */
    typedef void (*uota_progress_cb_t)(uint8_t progress, void *user_data);

    /**
     * @brief 错误回调
     * @param error_code 错误码
     * @param msg        错误消息
     * @param user_data  用户数据
     */
    typedef void (*uota_error_cb_t)(int error_code, const char *msg,
                                    void *user_data);

    /* ============================================================
     * API 函数
     * ============================================================ */

    /**
     * @brief 计算 CRC-16/MODBUS (原有自定义协议)
     *
     * @param data 数据指针
     * @param len  数据长度
     * @return CRC16 值
     */
    uint16_t uota_crc16(const uint8_t *data, size_t len);

    /**
     * @brief 计算 CRC-16/CCITT (YModem 标准)
     *
     * YModem 协议使用 CRC-16/CCITT (多项式 0x1021, 初始值 0x0000)。
     *
     * @param data 数据指针
     * @param len  数据长度
     * @return CRC16 值
     */
    uint16_t uota_crc16_ccitt(const uint8_t *data, size_t len);

    /**
     * @brief 构建数据包到缓冲区
     *
     * @param buf    输出缓冲区
     * @param size   缓冲区大小
     * @param seq    包序号
     * @param type   包类型
     * @param data   数据
     * @param len    数据长度
     * @return 构建的包总大小, 负值表示缓冲区不足
     */
    int uota_build_packet(uint8_t *buf, size_t size,
                          uint16_t seq, uota_pkt_type_t type,
                          const uint8_t *data, uint16_t len);

    /**
     * @brief 构建 ACK/NACK 包
     *
     * @param buf     输出缓冲区
     * @param size    缓冲区大小
     * @param seq     对应的包序号
     * @param ack_code 应答码
     * @return 构建的包总大小, 负值表示缓冲区不足
     */
    int uota_build_ack(uint8_t *buf, size_t size,
                       uint16_t seq, uota_ack_t ack_code);

    /**
     * @brief 构建命令包
     *
     * @param buf  输出缓冲区
     * @param size 缓冲区大小
     * @param seq  包序号
     * @param cmd  命令码
     * @param data 命令数据 (可为 NULL)
     * @param len  数据长度
     * @return 构建的包总大小, 负值表示缓冲区不足
     */
    int uota_build_cmd(uint8_t *buf, size_t size,
                       uint16_t seq, uota_cmd_t cmd,
                       const uint8_t *data, uint16_t len);

    /**
     * @brief 验证接收到的数据包
     *
     * @param buf  接收缓冲区
     * @param size 接收到的数据大小
     * @param pkt  输出解析后的包信息
     * @return 0 成功, 负值失败
     */
    int uota_parse_packet(const uint8_t *buf, size_t size,
                          uota_packet_t *pkt);

    /**
     * @brief 获取包类型名称字符串
     */
    const char *uota_type_str(uota_pkt_type_t type);

    /**
     * @brief 获取命令名称字符串
     */
    const char *uota_cmd_str(uota_cmd_t cmd);

    /**
     * @brief 获取应答码名称字符串
     */
    const char *uota_ack_str(uota_ack_t ack);

    /**
     * @brief 获取状态名称字符串
     */
    const char *uota_state_str(uota_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* __UART_OTA_PROTOCOL_H__ */
