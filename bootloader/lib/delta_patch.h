/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file delta_patch.h
 * @brief HDiffPatch 差分还原封装层
 *
 * 封装 HDiffPatch 的 patch_single_stream() API，
 * 提供适合嵌入式 Flash OTA 场景的差分还原接口。
 */

#ifndef __DELTA_PATCH_H__
#define __DELTA_PATCH_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================
 * 配置常量
 * ============================================================ */

/** 差分还原工作缓冲区默认大小 (用于 I/O 缓存) */
#define DELTA_PATCH_WORK_BUF_SIZE (1024 * 8)

/** 差分还原最小工作缓冲区 */
#define DELTA_PATCH_MIN_BUF_SIZE 2048

    /* ============================================================
     * 回调类型
     * ============================================================ */

    typedef void (*delta_progress_cb_t)(uint8_t progress, void *user_data);

    /* ============================================================
     * 配置结构
     * ============================================================ */

    typedef struct
    {
        uint32_t old_image_addr;         /**< 旧固件 Flash 地址 (slot0) */
        uint32_t old_image_size;         /**< 旧固件大小 (字节) */
        const uint8_t *patch_data;       /**< 补丁数据指针 (RAM) */
        uint32_t patch_size;             /**< 补丁数据大小 (字节) */
        uint32_t new_image_addr;         /**< 新固件目标 Flash 地址 (slot1) */
        uint32_t new_image_max_size;     /**< 目标区域最大大小 (字节) */
        uint8_t *work_buffer;            /**< 工作缓冲区 */
        uint32_t work_buffer_size;       /**< 工作缓冲区大小 */
        delta_progress_cb_t progress_cb; /**< 进度回调 (可选) */
        void *user_data;                 /**< 用户数据 */
    } delta_config_t;

    /* ============================================================
     * API 函数
     * ============================================================ */

    /**
     * @brief 获取差分还原后的新固件大小
     */
    int delta_patch_get_new_size(const uint8_t *patch_data,
                                 uint32_t patch_size,
                                 uint32_t *out_new_size);

    /**
     * @brief 执行差分还原 (使用 HDiffPatch)
     */
    int delta_patch_apply(const delta_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* __DELTA_PATCH_H__ */
