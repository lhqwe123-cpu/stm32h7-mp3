/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mcuboot_upgrade.h
 * @brief MCUboot 固件升级应用层接口
 *
 * 提供固件升级的 UI 界面和交互逻辑，包括：
 * - 从 SD 卡读取固件包
 * - 解析固件包头部信息
 * - 将固件写入 slot 1
 * - 请求 MCUboot 执行升级
 * - 升级状态监控和确认
 */

#ifndef __MCUBOOT_UPGRADE_H__
#define __MCUBOOT_UPGRADE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "fwpkg_parser.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================
 * 固件包文件格式定义
 *
 * 固件包 (.fwpkg) 文件结构:
 *   [Header: 64 bytes]
 *   [Firmware Image: variable]
 *
 * Header 结构:
 *   magic       : 4 bytes  - 魔数 "FWPK"
 *   version     : 4 bytes  - 固件版本 (major.minor.revision.build)
 *   image_size  : 4 bytes  - 固件镜像大小
 *   image_hash  : 32 bytes - SHA256 哈希
 *   reserved    : 20 bytes - 保留
 * ============================================================ */

/** 固件包魔数 */
#define FWPKG_MAGIC 0x4B505746 /* "FWPK" (little-endian) */

/** 固件包头部大小 */
#define FWPKG_HEADER_SIZE 64

/** 固件包文件扩展名 */
#define FWPKG_EXTENSION ".fwpkg"

/** SD 卡固件包扫描目录 */
#define FWPKG_SCAN_DIR "/SD:/FIRMWARE"

    /* ============================================================
     * 升级状态枚举
     * ============================================================ */

    /** 升级过程状态 */
    typedef enum
    {
        UPGRADE_STATE_IDLE,           /* 空闲, 未开始升级 */
        UPGRADE_STATE_SCANNING,       /* 正在扫描固件包 */
        UPGRADE_STATE_READY,          /* 已找到固件包, 等待确认 */
        UPGRADE_STATE_ERASING,        /* 正在擦除 slot 1 */
        UPGRADE_STATE_WRITING,        /* 正在写入固件 */
        UPGRADE_STATE_VERIFYING,      /* 正在验证写入 */
        UPGRADE_STATE_REQUESTING,     /* 正在请求 MCUboot 执行升级 */
        UPGRADE_STATE_PENDING_REBOOT, /* 等待重启以完成升级 */
        UPGRADE_STATE_COMPLETE,       /* 升级完成 (重启后确认) */
        UPGRADE_STATE_FAILED,         /* 升级失败 */
    } upgrade_state_t;

    /** 升级结果 */
    typedef enum
    {
        UPGRADE_RESULT_OK,             /* 成功 */
        UPGRADE_RESULT_NO_PACKAGE,     /* 未找到固件包 */
        UPGRADE_RESULT_BAD_HEADER,     /* 固件包头部无效 */
        UPGRADE_RESULT_SIZE_MISMATCH,  /* 固件大小不匹配 */
        UPGRADE_RESULT_ERASE_FAILED,   /* 擦除失败 */
        UPGRADE_RESULT_WRITE_FAILED,   /* 写入失败 */
        UPGRADE_RESULT_VERIFY_FAILED,  /* 验证失败 */
        UPGRADE_RESULT_REQUEST_FAILED, /* 升级请求失败 */
        UPGRADE_RESULT_SLOT_TOO_SMALL, /* slot 空间不足 */
    } upgrade_result_t;

    /* ============================================================
     * 升级进度回调
     * ============================================================ */

    /**
     * @brief 升级进度回调函数类型
     * @param state    当前升级状态
     * @param progress 进度百分比 (0-100)
     * @param user_data 用户自定义数据
     */
    typedef void (*upgrade_progress_cb_t)(upgrade_state_t state,
                                          uint8_t progress,
                                          void *user_data);

    /* ============================================================
     * API 函数
     * ============================================================ */

    /**
     * @brief 扫描 SD 卡上的固件包
     *
     * 扫描 FWPKG_SCAN_DIR 目录，查找 .fwpkg 文件。
     *
     * @param out_path  输出固件包完整路径 (缓冲区至少 256 字节)
     * @param path_size 路径缓冲区大小
     * @return 0 成功, 负值失败
     */
    int mcuboot_upgrade_scan_package(char *out_path, size_t path_size);

    /**
     * @brief 读取并解析固件包头部
     *
     * @param path   固件包文件路径
     * @param header 输出头部信息
     * @return 0 成功, 负值失败
     */
    int mcuboot_upgrade_read_header(const char *path, fwpkg_header_t *header);

    /**
     * @brief 执行固件升级流程
     *
     * 完整的升级流程: 擦除 slot1 -> 写入固件 -> 验证 -> 请求升级
     *
     * @param path     固件包文件路径
     * @param header   固件包头部信息
     * @param cb       进度回调 (可为 NULL)
     * @param user_data 用户数据 (可为 NULL)
     * @return 0 成功, 负值失败
     */
    int mcuboot_upgrade_perform(const char *path,
                                const fwpkg_header_t *header,
                                upgrade_progress_cb_t cb,
                                void *user_data);

    /**
     * @brief 确认当前运行的固件
     *
     * 调用此函数将当前固件标记为永久有效，防止 MCUboot 回滚。
     *
     * @return 0 成功, 负值失败
     */
    int mcuboot_upgrade_confirm(void);

    /**
     * @brief 获取当前升级状态
     *
     * @return 当前升级状态
     */
    upgrade_state_t mcuboot_upgrade_get_state(void);

    /**
     * @brief 获取当前运行的固件版本
     *
     * @param version 输出版本号 (格式: major<<24|minor<<16|revision<<8|build)
     * @return 0 成功, 负值失败
     */
    int mcuboot_upgrade_get_running_version(uint32_t *version);

    /**
     * @brief 获取 slot 1 中的固件版本
     *
     * @param version 输出版本号
     * @return 0 成功, 负值失败
     */
    int mcuboot_upgrade_get_slot1_version(uint32_t *version);

    /**
     * @brief 检查是否有待处理的升级
     *
     * @return true 有待升级, false 无
     */
    bool mcuboot_upgrade_has_pending(void);

    /**
     * @brief 启动固件升级 LVGL 界面
     *
     * 显示固件升级 UI，包含固件包列表、升级进度条等。
     *
     * @return 0 成功, 负值失败
     */
    int mcuboot_upgrade_ui_start(void);

#ifdef __cplusplus
}
#endif

#endif /* __MCUBOOT_UPGRADE_H__ */
