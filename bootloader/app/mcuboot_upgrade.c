/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mcuboot_upgrade.c
 * @brief MCUboot 固件升级应用层实现
 *
 * 实现固件升级的完整流程：
 * 1. 从 SD 卡扫描 .fwpkg 固件包
 * 2. 解析固件包头部
 * 3. 将固件写入 slot 1 (secondary slot)
 * 4. 请求 MCUboot 在下次启动时执行升级
 * 5. 升级状态监控和确认
 */

#include "mcuboot_upgrade.h"
#include "delta_patch.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/crc.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "my_malloc.h" /* mymalloc/myfree 从 SDRAM 分配 */

LOG_MODULE_REGISTER(mcuboot_upgrade, LOG_LEVEL_INF);

/* ============================================================
 * 内部状态
 * ============================================================ */

static upgrade_state_t g_upgrade_state = UPGRADE_STATE_IDLE;

/* ============================================================
 * 辅助函数
 * ============================================================ */

/** 判断文件是否为 .fwpkg 或 .fwp 扩展名 */
static bool has_fwpkg_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL)
    {
        return false;
    }
    return (strcmp(dot, FWPKG_EXTENSION) == 0 ||
            strcmp(dot, ".FWP") == 0);
}

/** 通知进度回调 */
static void notify_progress(upgrade_progress_cb_t cb, void *user_data,
                            upgrade_state_t state, uint8_t progress)
{
    g_upgrade_state = state;
    if (cb)
    {
        cb(state, progress, user_data);
    }
}

/* ============================================================
 * 固件包扫描
 * ============================================================ */

int mcuboot_upgrade_scan_package(char *out_path, size_t path_size)
{
    struct fs_dir_t dir;
    struct fs_dirent ent;
    int ret;

    if (!out_path || path_size == 0)
    {
        return -EINVAL;
    }

    g_upgrade_state = UPGRADE_STATE_SCANNING;

    fs_dir_t_init(&dir);
    ret = fs_opendir(&dir, FWPKG_SCAN_DIR);
    if (ret != 0)
    {
        LOG_WRN("Cannot open firmware dir %s (err: %d)", FWPKG_SCAN_DIR, ret);
        g_upgrade_state = UPGRADE_STATE_IDLE;
        return -ENOENT;
    }

    while (true)
    {
        memset(&ent, 0, sizeof(ent));
        ret = fs_readdir(&dir, &ent);
        if (ret != 0)
        {
            break;
        }
        if (ent.name[0] == '\0')
        {
            break;
        }
        if (ent.type != FS_DIR_ENTRY_FILE)
        {
            continue;
        }
        if (!has_fwpkg_ext(ent.name))
        {
            continue;
        }

        (void)snprintf(out_path, path_size, "%s/%s",
                       FWPKG_SCAN_DIR, ent.name);
        fs_closedir(&dir);

        LOG_INF("Found fwpkg: %s", out_path);
        g_upgrade_state = UPGRADE_STATE_READY;
        return 0;
    }

    fs_closedir(&dir);
    LOG_WRN("No .fwpkg found");
    g_upgrade_state = UPGRADE_STATE_IDLE;
    return -ENOENT;
}

/* ============================================================
 * 固件包头部解析
 * ============================================================ */

int mcuboot_upgrade_read_header(const char *path, fwpkg_header_t *header)
{
    struct fs_file_t file;
    ssize_t rd;
    int ret;

    if (!path || !header)
    {
        return -EINVAL;
    }

    fs_file_t_init(&file);
    ret = fs_open(&file, path, FS_O_READ);
    if (ret != 0)
    {
        LOG_ERR("Cannot open fwpkg %s (err: %d)", path, ret);
        return ret;
    }

    rd = fs_read(&file, header, sizeof(fwpkg_header_t));
    fs_close(&file);

    if (rd != sizeof(fwpkg_header_t))
    {
        LOG_ERR("Read header failed (read: %d, expected: %d)",
                (int)rd, (int)sizeof(fwpkg_header_t));
        return -EIO;
    }

    /* 验证魔数 */
    if (header->magic != FWPKG_MAGIC)
    {
        LOG_ERR("Bad magic: 0x%08X (expected: 0x%08X)",
                header->magic, FWPKG_MAGIC);
        return -EINVAL;
    }

    /* 验证镜像大小 */
    if (header->image_size == 0)
    {
        LOG_ERR("Image size is 0");
        return -EINVAL;
    }

    LOG_INF("Fwpkg header parsed:");
    LOG_INF("  Version: %u.%u.%u (build %u)",
            (header->version >> 24) & 0xFF,
            (header->version >> 16) & 0xFF,
            (header->version >> 8) & 0xFF,
            header->version & 0xFF);
    LOG_INF("  Image size: %u bytes", header->image_size);

    return 0;
}

/* ============================================================
 * 固件升级执行
 * ============================================================ */

int mcuboot_upgrade_perform(const char *path,
                            const fwpkg_header_t *header,
                            upgrade_progress_cb_t cb,
                            void *user_data)
{
    struct fs_file_t file;
    const struct flash_area *fa;
    uint8_t buf[512];
    uint32_t total_written = 0;
    uint32_t slot_size;
    off_t slot_off = 0;
    ssize_t rd;
    int ret;

    if (!path || !header)
    {
        return -EINVAL;
    }

    /* ---- 检测是否为差分升级包 ---- */
    if (header->type == FWPKG_TYPE_DELTA)
    {
        LOG_INF("Delta OTA package detected!");
        return mcuboot_upgrade_perform_delta(path, header, cb, user_data);
    }

    /* ---- 全量升级路径 (原有逻辑) ---- */

    /* ---- 1. 获取 slot 1 信息 ---- */
    ret = flash_area_open(PARTITION_ID(slot1_partition), &fa);
    if (ret != 0)
    {
        LOG_ERR("Cannot open slot 1 partition (err: %d)", ret);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return UPGRADE_RESULT_REQUEST_FAILED;
    }

    slot_size = fa->fa_size;
    LOG_INF("Slot 1 size: %u bytes", slot_size);

    /* 检查空间 */
    if (header->image_size > slot_size)
    {
        LOG_ERR("Image size %u exceeds slot 1 capacity %u",
                header->image_size, slot_size);
        flash_area_close(fa);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return UPGRADE_RESULT_SLOT_TOO_SMALL;
    }

    /* ---- 2. 擦除 slot 1 ---- */
    LOG_INF("Erasing slot 1...");
    notify_progress(cb, user_data, UPGRADE_STATE_ERASING, 0);

    ret = flash_area_erase(fa, 0, slot_size);
    if (ret != 0)
    {
        LOG_ERR("Erase slot 1 failed (err: %d)", ret);
        flash_area_close(fa);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return UPGRADE_RESULT_ERASE_FAILED;
    }

    LOG_INF("Slot 1 erased");

    /* ---- 3. 写入固件 ---- */
    fs_file_t_init(&file);
    ret = fs_open(&file, path, FS_O_READ);
    if (ret != 0)
    {
        LOG_ERR("Cannot open fwpkg (err: %d)", ret);
        flash_area_close(fa);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return UPGRADE_RESULT_WRITE_FAILED;
    }

    /* 跳过头部 */
    ret = fs_seek(&file, FWPKG_HEADER_SIZE, FS_SEEK_SET);
    if (ret != 0)
    {
        LOG_ERR("Seek past header failed (err: %d)", ret);
        fs_close(&file);
        flash_area_close(fa);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return UPGRADE_RESULT_WRITE_FAILED;
    }

    LOG_INF("Writing firmware...");
    notify_progress(cb, user_data, UPGRADE_STATE_WRITING, 0);

    while (total_written < header->image_size)
    {
        uint32_t remaining = header->image_size - total_written;
        uint32_t to_read = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;

        rd = fs_read(&file, buf, to_read);
        if (rd <= 0)
        {
            LOG_ERR("Read fwpkg failed (err: %d)", (int)rd);
            fs_close(&file);
            flash_area_close(fa);
            notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
            return UPGRADE_RESULT_WRITE_FAILED;
        }

        ret = flash_area_write(fa, slot_off, buf, rd);
        if (ret != 0)
        {
            LOG_ERR("Write slot 1 failed @ offset %ld (err: %d)",
                    (long)slot_off, ret);
            fs_close(&file);
            flash_area_close(fa);
            notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
            return UPGRADE_RESULT_WRITE_FAILED;
        }

        slot_off += rd;
        total_written += rd;

        /* 更新进度 */
        uint8_t progress = (uint8_t)((uint64_t)total_written * 100 /
                                     header->image_size);
        notify_progress(cb, user_data, UPGRADE_STATE_WRITING, progress);

        /* 让出 CPU */
        k_yield();
    }

    fs_close(&file);
    LOG_INF("Firmware written (%u bytes)", total_written);

    /* ---- 4. 请求 MCUboot 执行升级 ---- */
    flash_area_close(fa);
    LOG_INF("Requesting MCUboot upgrade...");
    notify_progress(cb, user_data, UPGRADE_STATE_REQUESTING, 98);

    ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
    if (ret != 0)
    {
        LOG_ERR("Upgrade request failed (err: %d)", ret);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return UPGRADE_RESULT_REQUEST_FAILED;
    }

    LOG_INF("Upgrade requested! Reboot to complete.");
    notify_progress(cb, user_data, UPGRADE_STATE_PENDING_REBOOT, 100);

    return 0;
}

/* ============================================================
 * 差分升级执行 (Delta OTA)
 * ============================================================ */

/** 差分还原进度回调适配 */
static void delta_upgrade_progress_cb(uint8_t progress, void *user_data)
{
    /* user_data 是 upgrade_progress_cb_t */
    upgrade_progress_cb_t cb = (upgrade_progress_cb_t)user_data;
    if (cb)
    {
        cb(UPGRADE_STATE_WRITING, progress, NULL);
    }
}

int mcuboot_upgrade_perform_delta(const char *path,
                                  const fwpkg_header_t *header,
                                  upgrade_progress_cb_t cb,
                                  void *user_data)
{
    struct fs_file_t file;
    uint8_t *patch_buf = NULL;
    uint32_t patch_size;
    ssize_t rd;
    int ret;

    LOG_INF("=== Delta OTA Upgrade ===");
    LOG_INF("Base version: %u.%u.%u.%u",
            header->base_version[0], header->base_version[1],
            header->base_version[2], header->base_version[3]);

    /* 1. 校验当前运行版本是否匹配 base_version */
    uint32_t running_ver = 0;
    if (mcuboot_upgrade_get_running_version(&running_ver) == 0)
    {
        uint32_t base_ver = ((uint32_t)header->base_version[0] << 24) |
                            ((uint32_t)header->base_version[1] << 16) |
                            ((uint32_t)header->base_version[2] << 8) |
                            header->base_version[3];
        if (running_ver != base_ver)
        {
            LOG_ERR("Version mismatch! Running: %u.%u.%u.%u, Base: %u.%u.%u.%u",
                    (running_ver >> 24) & 0xFF, (running_ver >> 16) & 0xFF,
                    (running_ver >> 8) & 0xFF, running_ver & 0xFF,
                    header->base_version[0], header->base_version[1],
                    header->base_version[2], header->base_version[3]);
            notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
            return -EINVAL;
        }
        LOG_INF("Base version matches running firmware");
    }
    else
    {
        LOG_WRN("Cannot read running version, skip version check");
    }

    /* 2. 读取补丁数据到 RAM */
    patch_size = header->image_size;
    LOG_INF("Patch size: %u bytes", patch_size);

    patch_buf = (uint8_t *)mymalloc(SRAMEX, patch_size);
    if (!patch_buf)
    {
        LOG_ERR("Failed to allocate %u bytes for patch", patch_size);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return -ENOMEM;
    }

    fs_file_t_init(&file);
    ret = fs_open(&file, path, FS_O_READ);
    if (ret != 0)
    {
        LOG_ERR("Cannot open fwpkg (err: %d)", ret);
        myfree(SRAMEX, patch_buf);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return ret;
    }

    /* 跳过头部 */
    ret = fs_seek(&file, FWPKG_HEADER_SIZE, FS_SEEK_SET);
    if (ret != 0)
    {
        LOG_ERR("Seek past header failed (err: %d)", ret);
        fs_close(&file);
        myfree(SRAMEX, patch_buf);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return ret;
    }

    rd = fs_read(&file, patch_buf, patch_size);
    fs_close(&file);

    if (rd != (ssize_t)patch_size)
    {
        LOG_ERR("Read patch failed: %d / %u", (int)rd, patch_size);
        myfree(SRAMEX, patch_buf);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return -EIO;
    }

    LOG_INF("Patch data loaded to RAM");

    /* 3. 获取 slot0 地址和大小 */
    const struct flash_area *fa_slot0;
    ret = flash_area_open(PARTITION_ID(slot0_partition), &fa_slot0);
    if (ret != 0)
    {
        LOG_ERR("Cannot open slot0 (err: %d)", ret);
        myfree(SRAMEX, patch_buf);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return ret;
    }

    uint32_t slot0_addr = fa_slot0->fa_off;
    uint32_t slot0_size = fa_slot0->fa_size;
    flash_area_close(fa_slot0);

    /* 4. 获取 slot1 地址和大小 */
    const struct flash_area *fa_slot1;
    ret = flash_area_open(PARTITION_ID(slot1_partition), &fa_slot1);
    if (ret != 0)
    {
        LOG_ERR("Cannot open slot1 (err: %d)", ret);
        myfree(SRAMEX, patch_buf);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return ret;
    }

    uint32_t slot1_addr = fa_slot1->fa_off;
    uint32_t slot1_size = fa_slot1->fa_size;
    flash_area_close(fa_slot1);

    /* 5. 执行差分还原 (delta_patch_apply 内部自行分配工作缓冲区) */
    notify_progress(cb, user_data, UPGRADE_STATE_ERASING, 0);

    delta_config_t dcfg = {
        .old_image_addr = slot0_addr,
        .old_image_size = slot0_size,
        .patch_data = patch_buf,
        .patch_size = patch_size,
        .new_image_addr = slot1_addr,
        .new_image_max_size = slot1_size,
        .work_buffer = NULL, /* 让 delta_patch_apply 自行分配 */
        .work_buffer_size = 0,
        .progress_cb = delta_upgrade_progress_cb,
        .user_data = (void *)cb,
    };

    ret = delta_patch_apply(&dcfg);

    myfree(SRAMEX, patch_buf);

    if (ret != 0)
    {
        LOG_ERR("Delta patch apply failed: %d", ret);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return ret;
    }

    /* 7. 请求 MCUboot 升级 */
    LOG_INF("Requesting MCUboot upgrade...");
    notify_progress(cb, user_data, UPGRADE_STATE_REQUESTING, 98);

    ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
    if (ret != 0)
    {
        LOG_ERR("Upgrade request failed (err: %d)", ret);
        notify_progress(cb, user_data, UPGRADE_STATE_FAILED, 0);
        return UPGRADE_RESULT_REQUEST_FAILED;
    }

    LOG_INF("Delta upgrade requested! Reboot to complete.");
    notify_progress(cb, user_data, UPGRADE_STATE_PENDING_REBOOT, 100);

    return 0;
}

/* ============================================================
 * 升级确认
 * ============================================================ */

int mcuboot_upgrade_confirm(void)
{
    int ret;

    ret = boot_write_img_confirmed();
    if (ret != 0)
    {
        LOG_ERR("Confirm failed (err: %d)", ret);
        return ret;
    }

    LOG_INF("Current firmware confirmed permanent");
    g_upgrade_state = UPGRADE_STATE_COMPLETE;
    return 0;
}

/* ============================================================
 * 状态查询
 * ============================================================ */

upgrade_state_t mcuboot_upgrade_get_state(void)
{
    return g_upgrade_state;
}

int mcuboot_upgrade_get_running_version(uint32_t *version)
{
    struct mcuboot_img_header header;
    int ret;

    if (!version)
    {
        return -EINVAL;
    }

    ret = boot_read_bank_header(boot_fetch_active_slot(),
                                &header, sizeof(header));
    if (ret != 0)
    {
        LOG_ERR("Read running header failed (err: %d)", ret);
        return ret;
    }

    if (header.mcuboot_version == 1)
    {
        *version = ((uint32_t)header.h.v1.sem_ver.major << 24) |
                   ((uint32_t)header.h.v1.sem_ver.minor << 16) |
                   ((uint32_t)header.h.v1.sem_ver.revision << 8) |
                   (header.h.v1.sem_ver.build_num & 0xFF);
        return 0;
    }

    return -ENOTSUP;
}

int mcuboot_upgrade_get_slot1_version(uint32_t *version)
{
    struct mcuboot_img_header header;
    int ret;

    if (!version)
    {
        return -EINVAL;
    }

    ret = boot_read_bank_header(PARTITION_ID(slot1_partition),
                                &header, sizeof(header));
    if (ret != 0)
    {
        LOG_DBG("Read slot 1 header failed (may be empty) (err: %d)", ret);
        return ret;
    }

    if (header.mcuboot_version == 1)
    {
        *version = ((uint32_t)header.h.v1.sem_ver.major << 24) |
                   ((uint32_t)header.h.v1.sem_ver.minor << 16) |
                   ((uint32_t)header.h.v1.sem_ver.revision << 8) |
                   (header.h.v1.sem_ver.build_num & 0xFF);
        return 0;
    }

    return -ENOTSUP;
}

bool mcuboot_upgrade_has_pending(void)
{
    int swap_type = mcuboot_swap_type();

    return (swap_type == BOOT_SWAP_TYPE_TEST ||
            swap_type == BOOT_SWAP_TYPE_PERM ||
            swap_type == BOOT_SWAP_TYPE_REVERT);
}

/* ============================================================
 * LVGL 升级界面 (简化版)
 * ============================================================ */

#include "lvgl.h"

/* UI 对象 */
static lv_obj_t *g_upgrade_scr = NULL;
static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_progress_bar = NULL;
static lv_obj_t *g_version_label = NULL;
static lv_obj_t *g_action_btn = NULL;
static lv_obj_t *g_back_btn = NULL;

static char g_fwpkg_path[256];
static fwpkg_header_t g_fwpkg_header;
static bool g_package_ready = false;

/* 升级进度回调 */
static void upgrade_ui_progress_cb(upgrade_state_t state,
                                   uint8_t progress,
                                   void *user_data)
{
    char buf[128];

    switch (state)
    {
    case UPGRADE_STATE_ERASING:
        lv_label_set_text(g_status_label, "Erasing slot...");
        lv_bar_set_value(g_progress_bar, 0, LV_ANIM_OFF);
        break;
    case UPGRADE_STATE_WRITING:
        snprintf(buf, sizeof(buf), "Writing... %u%%", progress);
        lv_label_set_text(g_status_label, buf);
        lv_bar_set_value(g_progress_bar, progress, LV_ANIM_ON);
        break;
    case UPGRADE_STATE_VERIFYING:
        lv_label_set_text(g_status_label, "Verifying...");
        lv_bar_set_value(g_progress_bar, 95, LV_ANIM_ON);
        break;
    case UPGRADE_STATE_REQUESTING:
        lv_label_set_text(g_status_label, "Requesting upgrade...");
        lv_bar_set_value(g_progress_bar, 98, LV_ANIM_ON);
        break;
    case UPGRADE_STATE_PENDING_REBOOT:
        lv_label_set_text(g_status_label, "Ready! Reboot to finish.");
        lv_bar_set_value(g_progress_bar, 100, LV_ANIM_ON);
        lv_obj_add_state(g_action_btn, LV_STATE_DISABLED);
        break;
    case UPGRADE_STATE_FAILED:
        lv_label_set_text(g_status_label, "Upgrade failed! Retry.");
        lv_bar_set_value(g_progress_bar, 0, LV_ANIM_OFF);
        break;
    default:
        break;
    }

    lv_task_handler();
}

/* 扫描固件包按钮回调 */
static void on_scan_click(lv_event_t *e)
{
    int ret;

    ret = mcuboot_upgrade_scan_package(g_fwpkg_path,
                                       sizeof(g_fwpkg_path));
    if (ret != 0)
    {
        lv_label_set_text(g_status_label, "No fwpkg found");
        g_package_ready = false;
        lv_obj_add_state(g_action_btn, LV_STATE_DISABLED);
        return;
    }

    ret = mcuboot_upgrade_read_header(g_fwpkg_path, &g_fwpkg_header);
    if (ret != 0)
    {
        lv_label_set_text(g_status_label, "Invalid fwpkg");
        g_package_ready = false;
        lv_obj_add_state(g_action_btn, LV_STATE_DISABLED);
        return;
    }

    /* 显示版本信息 */
    char buf[64];
    snprintf(buf, sizeof(buf), "New ver: %u.%u.%u (build %u)",
             (g_fwpkg_header.version >> 24) & 0xFF,
             (g_fwpkg_header.version >> 16) & 0xFF,
             (g_fwpkg_header.version >> 8) & 0xFF,
             g_fwpkg_header.version & 0xFF);
    lv_label_set_text(g_version_label, buf);
    lv_label_set_text(g_status_label, "Ready, press to upgrade");

    g_package_ready = true;
    lv_obj_clear_state(g_action_btn, LV_STATE_DISABLED);
    lv_label_set_text(g_action_btn, "Upgrade");
}

/* 升级按钮回调 */
static void on_upgrade_click(lv_event_t *e)
{
    if (!g_package_ready)
    {
        /* 扫描模式 */
        on_scan_click(e);
        return;
    }

    /* 执行升级 */
    lv_obj_add_state(g_action_btn, LV_STATE_DISABLED);
    lv_obj_add_state(g_back_btn, LV_STATE_DISABLED);

    int ret = mcuboot_upgrade_perform(g_fwpkg_path,
                                      &g_fwpkg_header,
                                      upgrade_ui_progress_cb,
                                      NULL);
    if (ret != 0)
    {
        LOG_ERR("Upgrade perform failed (err: %d)", ret);
        lv_obj_clear_state(g_back_btn, LV_STATE_DISABLED);
    }
}

/* 返回按钮回调 */
static void on_back_click(lv_event_t *e)
{
    if (g_upgrade_scr)
    {
        lv_obj_del(g_upgrade_scr);
        g_upgrade_scr = NULL;
    }
}

int mcuboot_upgrade_ui_start(void)
{
    uint32_t current_ver = 0;

    /* 创建屏幕 */
    g_upgrade_scr = lv_obj_create(NULL);
    lv_scr_load(g_upgrade_scr);

    /* 标题 */
    lv_obj_t *title = lv_label_create(g_upgrade_scr);
    lv_label_set_text(title, "Firmware Upgrade");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 当前版本 */
    char buf[64];
    if (mcuboot_upgrade_get_running_version(&current_ver) == 0)
    {
        snprintf(buf, sizeof(buf), "Cur ver: %u.%u.%u (build %u)",
                 (current_ver >> 24) & 0xFF,
                 (current_ver >> 16) & 0xFF,
                 (current_ver >> 8) & 0xFF,
                 current_ver & 0xFF);
    }
    else
    {
        snprintf(buf, sizeof(buf), "Cur ver: unknown");
    }

    lv_obj_t *cur_ver_label = lv_label_create(g_upgrade_scr);
    lv_label_set_text(cur_ver_label, buf);
    lv_obj_set_style_text_color(cur_ver_label, lv_color_white(), 0);
    lv_obj_align(cur_ver_label, LV_ALIGN_TOP_MID, 0, 40);

    /* 新版本信息 */
    g_version_label = lv_label_create(g_upgrade_scr);
    lv_label_set_text(g_version_label, "New ver: not scanned");
    lv_obj_set_style_text_color(g_version_label, lv_color_white(), 0);
    lv_obj_align(g_version_label, LV_ALIGN_TOP_MID, 0, 65);

    /* 状态标签 */
    g_status_label = lv_label_create(g_upgrade_scr);
    lv_label_set_text(g_status_label, "Press SCAN to find fwpkg");
    lv_obj_set_style_text_color(g_status_label, lv_color_white(), 0);
    lv_obj_align(g_status_label, LV_ALIGN_CENTER, 0, -20);

    /* 进度条 */
    g_progress_bar = lv_bar_create(g_upgrade_scr);
    lv_obj_set_size(g_progress_bar, 300, 20);
    lv_bar_set_range(g_progress_bar, 0, 100);
    lv_bar_set_value(g_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_align(g_progress_bar, LV_ALIGN_CENTER, 0, 10);

    /* 操作按钮 */
    g_action_btn = lv_btn_create(g_upgrade_scr);
    lv_obj_set_size(g_action_btn, 150, 40);
    lv_obj_align(g_action_btn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(g_action_btn, on_upgrade_click,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(g_action_btn);
    lv_label_set_text(btn_label, "Scan");
    lv_obj_center(btn_label);

    /* 返回按钮 */
    g_back_btn = lv_btn_create(g_upgrade_scr);
    lv_obj_set_size(g_back_btn, 80, 35);
    lv_obj_align(g_back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(g_back_btn, on_back_click,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(g_back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    g_package_ready = false;

    return 0;
}
