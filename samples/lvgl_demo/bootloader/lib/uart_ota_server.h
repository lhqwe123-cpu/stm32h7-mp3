/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file uart_ota_server.h
 * @brief 串口 OTA 服务端 (设备端接收固件) - 基于 YModem 协议
 *
 * 在设备端运行，通过 YModem 协议从串口接收固件数据。
 * 使用 Zephyr UART Async API (DMA) 实现高效数据收发。
 *
 * YModem 协议特性:
 *   - 握手协商 (发送 'C' 请求 CRC 模式)
 *   - 支持 128/256/1024 字节数据包
 *   - CRC-16/CCITT 校验
 *   - 自动重传请求 (ARQ)
 *   - 超时检测与恢复
 *   - 断点续传 (记录已接收偏移量)
 *   - 进度回调
 *   - 数据写入 Flash (slot1)
 *
 * 参考:
 *   - YModem 协议规范 (Chuck Forsberg)
 *   - RT-Thread ymodem 组件
 *   - lrzsz 开源实现
 */

#ifndef __UART_OTA_SERVER_H__
#define __UART_OTA_SERVER_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "uart_ota_protocol.h"
#include "fwpkg_parser.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================
 * OTA 服务端配置
 * ============================================================ */

/** 默认串口设备 (OTA专用, 独立于console的usart1) */
#define UOTA_DEFAULT_UART_DEV "usart3"

/** 默认波特率 */
#define UOTA_DEFAULT_BAUDRATE 115200

/** 接收环形缓冲区大小 */
#define UOTA_RING_BUF_SIZE 8192

/** 最大包大小 */
#define UOTA_MAX_PKT_SIZE (UOTA_MAX_PAYLOAD_SIZE + UOTA_OVERHEAD)

    /* ============================================================
     * OTA 服务端结构
     * ============================================================ */

    typedef struct
    {
        /* 配置 */
        const char *uart_dev_name; /**< 串口设备名称 */
        uint32_t baudrate;         /**< 波特率 */

        /* 状态 */
        uota_state_t state;        /**< 当前状态 */
        uota_start_info_t fw_info; /**< 固件信息 */
        uint8_t expected_seq;      /**< 期望的下一个序号 (YModem: 0-255循环) */
        uint32_t bytes_received;   /**< 已接收字节数 */
        uint32_t total_bytes;      /**< 总字节数 */
        bool transfer_active;      /**< 传输进行中 */

        /* 统计 */
        uota_stats_t stats;

        /* 回调 */
        uota_state_cb_t state_cb;       /**< 状态回调 */
        uota_progress_cb_t progress_cb; /**< 进度回调 */
        uota_error_cb_t error_cb;       /**< 错误回调 */
        void *user_data;                /**< 用户数据 */

        /* 内部状态 */
        bool initialized; /**< 是否已初始化 */
    } uota_server_t;

    /* ============================================================
     * API 函数
     * ============================================================ */

    /**
     * @brief 初始化串口 OTA 服务端
     *
     * @param server    服务端结构指针
     * @param uart_dev  串口设备名称 (如 "USART_1"), NULL 使用默认
     * @param baudrate  波特率, 0 使用默认 115200
     * @return 0 成功, 负值失败
     */
    int uota_server_init(uota_server_t *server,
                         const char *uart_dev,
                         uint32_t baudrate);

    /**
     * @brief 启动 OTA 接收 (阻塞式)
     *
     * 此函数会阻塞直到传输完成或失败。
     * 建议在独立线程中调用。
     *
     * @param server 服务端结构指针
     * @return 0 成功, 负值失败
     */
    int uota_server_start(uota_server_t *server);

    /**
     * @brief 中止 OTA 接收
     *
     * @param server 服务端结构指针
     * @return 0 成功, 负值失败
     */
    int uota_server_abort(uota_server_t *server);

    /**
     * @brief 获取传输统计信息
     *
     * @param server 服务端结构指针
     * @param stats  输出统计信息
     * @return 0 成功, 负值失败
     */
    int uota_server_get_stats(const uota_server_t *server,
                              uota_stats_t *stats);

    /**
     * @brief 获取当前状态
     *
     * @param server 服务端结构指针
     * @return 当前状态
     */
    uota_state_t uota_server_get_state(const uota_server_t *server);

    /**
     * @brief 获取接收进度
     *
     * @param server 服务端结构指针
     * @return 进度 (0-100)
     */
    uint8_t uota_server_get_progress(const uota_server_t *server);

    /**
     * @brief 设置状态变更回调
     *
     * @param server 服务端结构指针
     * @param cb     回调函数
     */
    void uota_server_set_state_cb(uota_server_t *server,
                                  uota_state_cb_t cb);

    /**
     * @brief 设置进度回调
     *
     * @param server 服务端结构指针
     * @param cb     回调函数
     */
    void uota_server_set_progress_cb(uota_server_t *server,
                                     uota_progress_cb_t cb);

    /**
     * @brief 设置错误回调
     *
     * @param server 服务端结构指针
     * @param cb     回调函数
     */
    void uota_server_set_error_cb(uota_server_t *server,
                                  uota_error_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __UART_OTA_SERVER_H__ */
