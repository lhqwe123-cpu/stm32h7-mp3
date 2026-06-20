/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file fwpkg_parser.h
 * @brief 固件包 (.fwpkg) 解析器
 *
 * 提供固件包的解析、验证功能。
 * 固件包格式:
 *   [Header: 64 bytes] [Firmware Image: variable]
 */

#ifndef __FWPKG_PARSER_H__
#define __FWPKG_PARSER_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================
 * 常量定义
 * ============================================================ */

/** 固件包魔数 "FWPK" */
#define FWPKG_MAGIC_VAL 0x4B505746

/** 固件包头部大小 */
#define FWPKG_HDR_SIZE 64

/** 固件包文件扩展名 */
#define FWPKG_FILE_EXT ".fwpkg"

/** 固件包扫描默认目录 */
#define FWPKG_DEFAULT_DIR "/SD:/FIRMWARE"

    /* ============================================================
     * 数据结构
     * ============================================================ */

    /** 固件包头部 */
    typedef struct
    {
        uint32_t magic;         /**< 魔数, 必须为 FWPKG_MAGIC_VAL */
        uint32_t version;       /**< 版本号 (major<<24|minor<<16|rev<<8|build) */
        uint32_t image_size;    /**< 固件镜像大小 (字节) */
        uint8_t image_hash[32]; /**< SHA256 哈希值 */
        uint8_t reserved[20];   /**< 保留字段 */
    } fwpkg_header_t;

    /** 固件包信息 (解析后的完整信息) */
    typedef struct
    {
        fwpkg_header_t header; /**< 原始头部 */
        char file_path[256];   /**< 文件路径 */
        uint32_t file_size;    /**< 文件总大小 */
        uint8_t major_ver;     /**< 主版本号 */
        uint8_t minor_ver;     /**< 次版本号 */
        uint16_t revision;     /**< 修订号 */
        uint32_t build_num;    /**< 构建号 */
    } fwpkg_info_t;

    /** 解析结果码 */
    typedef enum
    {
        FWPKG_OK = 0,                 /**< 成功 */
        FWPKG_ERR_NOT_FOUND = -1,     /**< 文件未找到 */
        FWPKG_ERR_READ_FAILED = -2,   /**< 读取失败 */
        FWPKG_ERR_BAD_MAGIC = -3,     /**< 魔数无效 */
        FWPKG_ERR_BAD_SIZE = -4,      /**< 大小无效 */
        FWPKG_ERR_HASH_MISMATCH = -5, /**< 哈希不匹配 */
        FWPKG_ERR_TOO_LARGE = -6,     /**< 固件过大 */
        FWPKG_ERR_PARAM = -7,         /**< 参数无效 */
    } fwpkg_result_t;

    /* ============================================================
     * API 函数
     * ============================================================ */

    /**
     * @brief 扫描目录查找固件包
     *
     * @param dir_path  扫描目录路径
     * @param out_path  输出找到的固件包完整路径
     * @param path_size 输出缓冲区大小
     * @return FWPKG_OK 成功, 负值失败
     */
    fwpkg_result_t fwpkg_scan(const char *dir_path,
                              char *out_path, size_t path_size);

    /**
     * @brief 解析固件包头部
     *
     * @param file_path 固件包文件路径
     * @param info      输出解析后的固件包信息
     * @return FWPKG_OK 成功, 负值失败
     */
    fwpkg_result_t fwpkg_parse_header(const char *file_path,
                                      fwpkg_info_t *info);

    /**
     * @brief 验证固件包完整性
     *
     * 读取固件包中的镜像数据并计算 SHA256，与头部中的哈希值比较。
     *
     * @param file_path 固件包文件路径
     * @return FWPKG_OK 成功, 负值失败
     */
    fwpkg_result_t fwpkg_verify(const char *file_path);

    /**
     * @brief 从固件包中读取镜像数据
     *
     * @param file_path 固件包文件路径
     * @param buf       输出缓冲区
     * @param offset    镜像数据偏移 (从镜像起始位置计算)
     * @param len       读取长度
     * @param out_read  实际读取字节数
     * @return FWPKG_OK 成功, 负值失败
     */
    fwpkg_result_t fwpkg_read_image_data(const char *file_path,
                                         uint8_t *buf,
                                         uint32_t offset,
                                         uint32_t len,
                                         uint32_t *out_read);

    /**
     * @brief 获取固件包中镜像数据的总大小
     *
     * @param file_path 固件包文件路径
     * @param out_size  输出镜像大小
     * @return FWPKG_OK 成功, 负值失败
     */
    fwpkg_result_t fwpkg_get_image_size(const char *file_path,
                                        uint32_t *out_size);

    /**
     * @brief 将结果码转换为可读字符串
     *
     * @param result 结果码
     * @return 描述字符串
     */
    const char *fwpkg_result_str(fwpkg_result_t result);

    /**
     * @brief 判断文件是否为固件包
     *
     * @param file_name 文件名
     * @return true 是固件包, false 不是
     */
    bool fwpkg_is_package_file(const char *file_name);

#ifdef __cplusplus
}
#endif

#endif /* __FWPKG_PARSER_H__ */
