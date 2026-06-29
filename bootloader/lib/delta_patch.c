/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file delta_patch.c
 * @brief HDiffPatch 差分还原实现
 *
 * 使用 HDiffPatch 的 patch_single_stream() API，
 * 通过 Zephyr flash_map 实现 Flash 读写流。
 */

#include "delta_patch.h"
#include "hdiffpatch/patch.h"

/* HDiffPatch 的 patch_types.h 定义了 LOG_ERR 宏 (fprintf),
   与 Zephyr logging 冲突, 先 undef 掉 */
#ifdef LOG_ERR
#undef LOG_ERR
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/devicetree.h>

#include <string.h>
#include <errno.h>

#include "my_malloc.h" /* mymalloc/myfree 从 SDRAM 分配, 不占用内部 SRAM */

LOG_MODULE_REGISTER(delta_patch, LOG_LEVEL_INF);

/* STM32H7 内部 Flash 基地址 (AXIM 映射) */
#define STM32H7_FLASH_BASE 0x08000000UL

/* ============================================================
 * Flash 读取流 (用于 oldData 随机读取)
 * ============================================================ */

typedef struct
{
    uint32_t flash_addr; /* Flash 基地址 */
    uint32_t flash_size; /* 可读取范围 */
} flash_read_ctx_t;

static hpatch_BOOL flash_read_func(const hpatch_TStreamInput *stream,
                                   hpatch_StreamPos_t readFromPos,
                                   unsigned char *out_data,
                                   unsigned char *out_data_end)
{
    flash_read_ctx_t *ctx = (flash_read_ctx_t *)stream->streamImport;
    hpatch_size_t readLen = (hpatch_size_t)(out_data_end - out_data);

    if (readFromPos + readLen > ctx->flash_size)
    {
        return hpatch_FALSE;
    }

    /* flash_addr 是 fa_off (Flash 设备内偏移), 需加上基地址才能直接寻址 */
    const uint8_t *src = (const uint8_t *)(STM32H7_FLASH_BASE + ctx->flash_addr + (uint32_t)readFromPos);
    memcpy(out_data, src, readLen);
    return hpatch_TRUE;
}

/* ============================================================
 * Flash 写入流 (用于 newData 顺序写入)
 * ============================================================ */

typedef struct
{
    uint32_t flash_addr;         /* Flash 基地址 */
    uint32_t flash_size;         /* 最大可写入范围 */
    uint32_t written;            /* 已写入字节数 */
    const struct flash_area *fa; /* flash_area 句柄 */
    delta_progress_cb_t progress_cb;
    void *user_data;
    uint32_t total_size; /* 总大小 (用于进度计算) */
} flash_write_ctx_t;

static hpatch_BOOL flash_write_func(const hpatch_TStreamOutput *stream,
                                    hpatch_StreamPos_t writeToPos,
                                    const unsigned char *data,
                                    const unsigned char *data_end)
{
    flash_write_ctx_t *ctx = (flash_write_ctx_t *)stream->streamImport;
    hpatch_size_t writeLen = (hpatch_size_t)(data_end - data);

    if (writeToPos + writeLen > ctx->flash_size)
    {
        return hpatch_FALSE;
    }

    /* 使用 flash_area_write 写入 Flash */
    int ret = flash_area_write(ctx->fa, (off_t)writeToPos, data, writeLen);
    if (ret != 0)
    {
        LOG_ERR("Flash write failed @ %llu, len=%u, err=%d",
                (unsigned long long)writeToPos, (unsigned)writeLen, ret);
        return hpatch_FALSE;
    }

    ctx->written += writeLen;

    /* 进度回调 */
    if (ctx->progress_cb && ctx->total_size > 0)
    {
        uint8_t pct = (uint8_t)(((uint64_t)ctx->written * 100) / ctx->total_size);
        ctx->progress_cb(pct, ctx->user_data);
    }

    return hpatch_TRUE;
}

/* ============================================================
 * 补丁数据读取流 (顺序读取, 从 RAM)
 * ============================================================ */

typedef struct
{
    const uint8_t *data;
    uint32_t size;
    uint32_t read_pos;
} patch_read_ctx_t;

static hpatch_BOOL patch_read_func(const hpatch_TStreamInput *stream,
                                   hpatch_StreamPos_t readFromPos,
                                   unsigned char *out_data,
                                   unsigned char *out_data_end)
{
    patch_read_ctx_t *ctx = (patch_read_ctx_t *)stream->streamImport;
    hpatch_size_t readLen = (hpatch_size_t)(out_data_end - out_data);

    if (readFromPos + readLen > ctx->size)
    {
        return hpatch_FALSE;
    }

    memcpy(out_data, ctx->data + (uint32_t)readFromPos, readLen);
    return hpatch_TRUE;
}

/* ============================================================
 * 公共 API
 * ============================================================ */

/* listener 回调: HDiffPatch 通过此回调获取工作缓冲区 */
static hpatch_BOOL delta_onDiffInfo(sspatch_listener_t *listener,
                                    const hpatch_singleCompressedDiffInfo *info,
                                    hpatch_TDecompress **out_decompressPlugin,
                                    unsigned char **out_temp_cache,
                                    unsigned char **out_temp_cacheEnd)
{
    /* 嵌入式环境不提供解压插件, 要求 PC 端使用 -c none 生成未压缩补丁 */
    if (info->compressedSize > 0)
    {
        LOG_ERR("Compressed diff not supported! Use hdiffz -c none on PC side.");
        return hpatch_FALSE;
    }
    *out_decompressPlugin = NULL;

    /* 从 listener->import 获取预分配的工作缓冲区 */
    unsigned char *buf = (unsigned char *)listener->import;
    if (!buf)
    {
        return hpatch_FALSE;
    }

    /* 工作缓冲区大小从 buf 的前 4 字节读取 (这 4 字节本身也占空间) */
    uint32_t buf_size = *(uint32_t *)buf;
    hpatch_StreamPos_t needSize = info->stepMemSize + (hpatch_StreamPos_t)hpatch_kStreamCacheSize * 3;

    if (buf_size < needSize)
    {
        LOG_ERR("Work buffer too small: %u < %llu needed (stepMem=%llu)",
                buf_size, (unsigned long long)needSize,
                (unsigned long long)info->stepMemSize);
        return hpatch_FALSE;
    }

    /* 跳过前 4 字节的大小头, 实际可用缓冲区为 buf+4 到 buf+4+buf_size */
    *out_temp_cache = buf + 4;
    *out_temp_cacheEnd = buf + 4 + buf_size;

    LOG_INF("Delta patch: stepMem=%llu buf=%u",
            (unsigned long long)info->stepMemSize, buf_size);

    return hpatch_TRUE;
}

int delta_patch_get_new_size(const uint8_t *patch_data,
                             uint32_t patch_size,
                             uint32_t *out_new_size)
{
    hpatch_singleCompressedDiffInfo diffInfo;
    hpatch_TStreamInput diffStream;

    if (!patch_data || !out_new_size || patch_size < 8)
    {
        return -EINVAL;
    }

    mem_as_hStreamInput(&diffStream, patch_data, patch_data + patch_size);

    if (!getSingleCompressedDiffInfo(&diffInfo, &diffStream, 0))
    {
        LOG_ERR("Failed to parse diff info");
        return -EINVAL;
    }

    *out_new_size = (uint32_t)diffInfo.newDataSize;
    LOG_INF("Delta patch: old=%llu new=%llu covers=%llu stepMem=%llu",
            (unsigned long long)diffInfo.oldDataSize,
            (unsigned long long)diffInfo.newDataSize,
            (unsigned long long)diffInfo.coverCount,
            (unsigned long long)diffInfo.stepMemSize);

    return 0;
}

int delta_patch_apply(const delta_config_t *cfg)
{
    if (!cfg || !cfg->patch_data)
    {
        return -EINVAL;
    }

    /* 1. 获取差分信息 (先解析, 确定所需缓冲区大小) */
    hpatch_singleCompressedDiffInfo diffInfo;
    {
        hpatch_TStreamInput diffStream;
        mem_as_hStreamInput(&diffStream, cfg->patch_data,
                            cfg->patch_data + cfg->patch_size);
        if (!getSingleCompressedDiffInfo(&diffInfo, &diffStream, 0))
        {
            LOG_ERR("Failed to parse diff info");
            return -EINVAL;
        }
    }

    LOG_INF("Delta apply: old=%llu new=%llu stepMem=%llu",
            (unsigned long long)diffInfo.oldDataSize,
            (unsigned long long)diffInfo.newDataSize,
            (unsigned long long)diffInfo.stepMemSize);

    /* 2. 校验大小 */
    if (diffInfo.newDataSize > cfg->new_image_max_size)
    {
        LOG_ERR("New image too large: %llu > %u",
                (unsigned long long)diffInfo.newDataSize,
                cfg->new_image_max_size);
        return -ENOSPC;
    }

    /* 3. 分配工作缓冲区 (stepMemSize + I/O cache) */
    hpatch_StreamPos_t needBufSize = diffInfo.stepMemSize +
                                     (hpatch_StreamPos_t)hpatch_kStreamCacheSize * 3 + 4;
    /* 如果调用者提供了足够大的 work_buffer, 使用它; 否则动态分配 */
    uint8_t *work_buf;
    bool work_buf_allocated = false;

    if (cfg->work_buffer && cfg->work_buffer_size >= (uint32_t)needBufSize)
    {
        work_buf = cfg->work_buffer;
    }
    else
    {
        LOG_INF("Allocating %llu bytes for patch work buffer", (unsigned long long)needBufSize);
        work_buf = (uint8_t *)mymalloc(SRAMEX, (uint32_t)needBufSize);
        if (!work_buf)
        {
            LOG_ERR("Failed to allocate work buffer (%llu bytes)",
                    (unsigned long long)needBufSize);
            return -ENOMEM;
        }
        work_buf_allocated = true;
    }

    /* 前 4 字节存储缓冲区大小 (不含这 4 字节本身), delta_onDiffInfo 会读取 */
    *(uint32_t *)work_buf = (uint32_t)(needBufSize - 4);

    /* 4. 打开 slot1 flash_area 并擦除 */
    const struct flash_area *fa;
    int ret = flash_area_open(PARTITION_ID(slot1_partition), &fa);
    if (ret != 0)
    {
        LOG_ERR("Cannot open slot1 (err: %d)", ret);
        if (work_buf_allocated)
            myfree(SRAMEX, work_buf);
        return ret;
    }

    LOG_INF("Erasing slot1 (%u bytes)...", (unsigned)fa->fa_size);
    ret = flash_area_erase(fa, 0, fa->fa_size);
    if (ret != 0)
    {
        LOG_ERR("Erase slot1 failed (err: %d)", ret);
        flash_area_close(fa);
        if (work_buf_allocated)
            myfree(SRAMEX, work_buf);
        return ret;
    }
    LOG_INF("Slot1 erased");

    /* 5. 构造流对象 */
    hpatch_TStreamInput oldStream;
    hpatch_TStreamOutput newStream;
    hpatch_TStreamInput diffStream;

    flash_read_ctx_t readCtx = {
        .flash_addr = cfg->old_image_addr,
        .flash_size = cfg->old_image_size,
    };

    flash_write_ctx_t writeCtx = {
        .flash_addr = cfg->new_image_addr,
        .flash_size = cfg->new_image_max_size,
        .written = 0,
        .fa = fa,
        .progress_cb = cfg->progress_cb,
        .user_data = cfg->user_data,
        .total_size = (uint32_t)diffInfo.newDataSize,
    };

    patch_read_ctx_t patchCtx = {
        .data = cfg->patch_data,
        .size = cfg->patch_size,
        .read_pos = 0,
    };

    /* oldData: 随机读取 Flash */
    oldStream.streamImport = &readCtx;
    oldStream.streamSize = diffInfo.oldDataSize; /* 使用 diff 中记录的 old 大小, 而非整个 slot */
    oldStream.read = flash_read_func;
    oldStream._private_reserved = NULL;

    /* newData: 顺序写入 Flash */
    newStream.streamImport = &writeCtx;
    newStream.streamSize = diffInfo.newDataSize;
    newStream.read_writed = NULL;
    newStream.write = flash_write_func;

    /* diffData: 顺序读取 RAM */
    diffStream.streamImport = &patchCtx;
    diffStream.streamSize = cfg->patch_size;
    diffStream.read = patch_read_func;
    diffStream._private_reserved = NULL;

    /* 6. 执行差分还原 */
    LOG_INF("Patching...");

    sspatch_listener_t listener;
    memset(&listener, 0, sizeof(listener));
    listener.onDiffInfo = delta_onDiffInfo;
    listener.import = work_buf; /* 工作缓冲区通过 import 传递 */

    hpatch_BOOL ok = patch_single_stream(
        &listener,   /* listener */
        &newStream,  /* out_newData */
        &oldStream,  /* oldData */
        &diffStream, /* singleCompressedDiff */
        0,           /* diffInfo_pos */
        NULL,        /* coversListener */
        1            /* threadNum (单线程) */
    );

    flash_area_close(fa);

    if (work_buf_allocated)
    {
        myfree(SRAMEX, work_buf);
    }

    if (!ok)
    {
        LOG_ERR("Delta patch failed!");
        return -EIO;
    }

    LOG_INF("Delta patch OK, wrote %u bytes", writeCtx.written);
    return 0;
}
