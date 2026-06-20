/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ymodem.h
 * @brief YModem-1K 协议实现 (Zephyr RTOS)
 *
 * YModem 协议规范:
 * - 支持 128 字节和 1024 字节两种数据包大小
 * - 使用 CRC-16/CCITT 校验
 * - 支持握手协商、批量传输、EOT 结束
 * - 支持断点续传 (通过记录已接收偏移量)
 *
 * 传输流程:
 *   1. 接收端发送 'C' (CRC模式请求)
 *   2. 发送端发送文件名包 (SOH 00 + 文件名+文件大小)
 *   3. 接收端发送 ACK + 'C'
 *   4. 发送端发送数据包 (STX 01 + 1024字节数据)
 *   5. 接收端发送 ACK
 *   6. 重复 4-5 直到所有数据发送完毕
 *   7. 发送端发送 EOT
 *   8. 接收端发送 NAK
 *   9. 发送端再次发送 EOT
 *   10. 接收端发送 ACK + 'C'
 *   11. 发送端发送空文件名包 (结束传输)
 *   12. 接收端发送 ACK
 *
 * 参考:
 *   - YModem 协议规范 (Chuck Forsberg)
 *   - XMODEM/YMODEM Protocol Reference
 */

#ifndef __YMODEM_H__
#define __YMODEM_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================
 * YModem 协议常量
 * ============================================================ */

/** 控制字符 */
#define YMODEM_SOH 0x01 /**< 128字节数据包头部 */
#define YMODEM_STX 0x02 /**< 1024字节数据包头部 */
#define YMODEM_EOT 0x04 /**< 传输结束 */
#define YMODEM_ACK 0x06 /**< 确认应答 */
#define YMODEM_NAK 0x15 /**< 否定应答 */
#define YMODEM_CAN 0x18 /**< 取消传输 */
#define YMODEM_C 0x43   /**< 'C' - CRC模式请求 */

/** 数据包大小 */
#define YMODEM_PACKET_SIZE_128 128   /**< SOH 包数据大小 */
#define YMODEM_PACKET_SIZE_256 256   /**< 扩展 256 字节包 (自定义 STX_256) */
#define YMODEM_PACKET_SIZE_1024 1024 /**< STX 包数据大小 */
#define YMODEM_PACKET_OVERHEAD 5     /**< 包头(3) + CRC(2) */
#define YMODEM_MAX_PACKET_SIZE (YMODEM_PACKET_SIZE_1024 + YMODEM_PACKET_OVERHEAD)

/** 文件名包中文件名字段大小 */
#define YMODEM_FILE_NAME_LEN 256
/** 文件名包中文件大小字段大小 */
#define YMODEM_FILE_SIZE_LEN 32

/** 超时定义 (毫秒) */
#define YMODEM_INIT_TIMEOUT_MS 60000  /**< 初始握手超时 (60秒) */
#define YMODEM_PACKET_TIMEOUT_MS 3000 /**< 数据包超时 (3秒) */
#define YMODEM_EOT_TIMEOUT_MS 10000   /**< EOT 超时 (10秒) */

/** 最大重试次数 */
#define YMODEM_MAX_RETRIES 10
/** 最大 CAN 发送次数 (取消传输) */
#define YMODEM_CAN_COUNT 5

/** 默认数据包大小 (使用 1024 字节 STX 模式) */
#define YMODEM_DEFAULT_PACKET_SIZE YMODEM_PACKET_SIZE_1024

    /* ============================================================
     * YModem 传输状态
     * ============================================================ */

    typedef enum
    {
        YMODEM_STATE_IDLE,           /**< 空闲 */
        YMODEM_STATE_HANDSHAKE,      /**< 握手中 (发送 'C') */
        YMODEM_STATE_RECEIVING_FILE, /**< 接收文件名包 */
        YMODEM_STATE_RECEIVING_DATA, /**< 接收数据包 */
        YMODEM_STATE_WAIT_EOT,       /**< 等待 EOT */
        YMODEM_STATE_COMPLETE,       /**< 传输完成 */
        YMODEM_STATE_FAILED,         /**< 传输失败 */
        YMODEM_STATE_ABORTED,        /**< 已中止 */
    } ymodem_state_t;

    /* ============================================================
     * YModem 错误码
     * ============================================================ */

    typedef enum
    {
        YMODEM_OK = 0,            /**< 成功 */
        YMODEM_ERR_TIMEOUT = -1,  /**< 超时 */
        YMODEM_ERR_CANCEL = -2,   /**< 对方取消 */
        YMODEM_ERR_CRC = -3,      /**< CRC 校验错误 */
        YMODEM_ERR_SEQ = -4,      /**< 序号错误 */
        YMODEM_ERR_FLASH = -5,    /**< Flash 写入错误 */
        YMODEM_ERR_MEMORY = -6,   /**< 内存不足 */
        YMODEM_ERR_PARAM = -7,    /**< 参数错误 */
        YMODEM_ERR_INTERNAL = -8, /**< 内部错误 */
        YMODEM_ERR_ABORTED = -9,  /**< 用户中止 */
    } ymodem_error_t;

    /* ============================================================
     * YModem 文件信息
     * ============================================================ */

    typedef struct
    {
        char file_name[YMODEM_FILE_NAME_LEN]; /**< 文件名 */
        uint32_t file_size;                   /**< 文件大小 */
        uint32_t bytes_received;              /**< 已接收字节数 */
        uint32_t total_packets;               /**< 总包数 */
        uint32_t received_packets;            /**< 已接收包数 */
        uint32_t retry_count;                 /**< 重试次数 */
        uint32_t crc_errors;                  /**< CRC 错误次数 */
        uint32_t seq_errors;                  /**< 序号错误次数 */
    } ymodem_file_info_t;

    /* ============================================================
     * 回调类型
     * ============================================================ */

    /**
     * @brief 状态变更回调
     * @param state    新状态
     * @param user_data 用户数据
     */
    typedef void (*ymodem_state_cb_t)(ymodem_state_t state, void *user_data);

    /**
     * @brief 进度回调
     * @param progress 进度 (0-100)
     * @param user_data 用户数据
     */
    typedef void (*ymodem_progress_cb_t)(uint8_t progress, void *user_data);

    /**
     * @brief 错误回调
     * @param error     错误码
     * @param msg       错误消息
     * @param user_data 用户数据
     */
    typedef void (*ymodem_error_cb_t)(int error, const char *msg, void *user_data);

    /* ============================================================
     * YModem 接收端结构
     * ============================================================ */

    typedef struct
    {
        /* 配置 */
        const char *uart_dev_name; /**< 串口设备名称 */
        uint32_t baudrate;         /**< 波特率 */
        uint16_t packet_size;      /**< 数据包大小 (128 或 1024) */

        /* 状态 */
        ymodem_state_t state;         /**< 当前状态 */
        ymodem_file_info_t file_info; /**< 文件信息 */
        uint8_t expected_seq;         /**< 期望的下一个序号 */
        bool transfer_active;         /**< 传输进行中 */

        /* 断点续传 */
        uint32_t resume_offset; /**< 续传偏移量 (0 = 从头开始) */
        bool resume_enabled;    /**< 是否启用断点续传 */

        /* 回调 */
        ymodem_state_cb_t state_cb;       /**< 状态回调 */
        ymodem_progress_cb_t progress_cb; /**< 进度回调 */
        ymodem_error_cb_t error_cb;       /**< 错误回调 */
        void *user_data;                  /**< 用户数据 */

        /* 内部状态 */
        bool initialized; /**< 是否已初始化 */
    } ymodem_receiver_t;

    /* ============================================================
     * API 函数
     * ============================================================ */

    /**
     * @brief 计算 CRC-16/CCITT (YModem 标准)
     *
     * @param data 数据指针
     * @param len  数据长度
     * @return CRC16 值
     */
    uint16_t ymodem_crc16(const uint8_t *data, size_t len);

    /**
     * @brief 初始化 YModem 接收端
     *
     * @param rx         接收端结构指针
     * @param uart_dev   串口设备名称 (如 "usart3"), NULL 使用默认
     * @param baudrate   波特率, 0 使用默认 115200
     * @param packet_size 数据包大小 (128 或 1024), 0 使用默认 1024
     * @return 0 成功, 负值失败
     */
    int ymodem_receiver_init(ymodem_receiver_t *rx,
                             const char *uart_dev,
                             uint32_t baudrate,
                             uint16_t packet_size);

    /**
     * @brief 启动 YModem 接收 (阻塞式)
     *
     * 此函数会阻塞直到传输完成或失败。
     * 建议在独立线程中调用。
     *
     * @param rx 接收端结构指针
     * @return 0 成功, 负值失败
     */
    int ymodem_receiver_start(ymodem_receiver_t *rx);

    /**
     * @brief 中止 YModem 接收
     *
     * @param rx 接收端结构指针
     * @return 0 成功, 负值失败
     */
    int ymodem_receiver_abort(ymodem_receiver_t *rx);

    /**
     * @brief 设置断点续传偏移量
     *
     * @param rx     接收端结构指针
     * @param offset 已接收的字节偏移量
     */
    void ymodem_receiver_set_resume_offset(ymodem_receiver_t *rx, uint32_t offset);

    /**
     * @brief 获取当前状态
     *
     * @param rx 接收端结构指针
     * @return 当前状态
     */
    ymodem_state_t ymodem_receiver_get_state(const ymodem_receiver_t *rx);

    /**
     * @brief 获取接收进度
     *
     * @param rx 接收端结构指针
     * @return 进度 (0-100)
     */
    uint8_t ymodem_receiver_get_progress(const ymodem_receiver_t *rx);

    /**
     * @brief 获取文件信息
     *
     * @param rx   接收端结构指针
     * @param info 输出文件信息
     * @return 0 成功, 负值失败
     */
    int ymodem_receiver_get_file_info(const ymodem_receiver_t *rx,
                                      ymodem_file_info_t *info);

    /**
     * @brief 设置状态变更回调
     */
    void ymodem_receiver_set_state_cb(ymodem_receiver_t *rx,
                                      ymodem_state_cb_t cb);

    /**
     * @brief 设置进度回调
     */
    void ymodem_receiver_set_progress_cb(ymodem_receiver_t *rx,
                                         ymodem_progress_cb_t cb);

    /**
     * @brief 设置错误回调
     */
    void ymodem_receiver_set_error_cb(ymodem_receiver_t *rx,
                                      ymodem_error_cb_t cb);

    /**
     * @brief 获取状态名称字符串
     */
    const char *ymodem_state_str(ymodem_state_t state);

    /**
     * @brief 获取错误名称字符串
     */
    const char *ymodem_error_str(ymodem_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* __YMODEM_H__ */
