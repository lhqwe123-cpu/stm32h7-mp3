#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/cache.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

LOG_MODULE_REGISTER(lvgl_video, LOG_LEVEL_INF);

#include "lvgl.h"
#include "lcd.h"
#include "my_malloc.h"
#include "jpegcodec.h"
#include "lvgl_sd_video.h"
#include "lvgl_sd_pic.h"

#define SD_VIDEO_SCAN_DIR "/SD:/VIDEO"
#define SD_VIDEO_MAX_FRAME_SIZE (1024U * 1024U)
#define SD_VIDEO_MAX_PATH 192U

/* 视频播放状态 */
typedef enum
{
    VIDEO_STATE_LIST,    /* 列表状态 */
    VIDEO_STATE_PLAYING, /* 播放状态 */
    VIDEO_STATE_PAUSED,  /* 暂停状态 */
} video_app_state_t;

typedef struct
{
    struct fs_file_t file;
    bool file_opened;
    char path[SD_VIDEO_MAX_PATH];

    uint32_t movi_data_off;
    uint32_t movi_data_end;
    uint32_t next_chunk_off;
    uint32_t rec_list_end;

    uint32_t us_per_frame;
    uint32_t total_frames;
    uint16_t frame_w;
    uint16_t frame_h;

    uint8_t *jpeg_buf;
    uint32_t jpeg_buf_cap;
    uint16_t *rgb_buf;

    lv_image_dsc_t img_dsc;
    lv_obj_t *img_obj;
    lv_obj_t *left_label;
    lv_obj_t *info_label;
    lv_timer_t *timer;

    /* 播放状态 */
    video_app_state_t state;
    uint32_t current_frame;
    lv_obj_t *progress_bar;
    lv_obj_t *pause_btn;
    lv_obj_t *pause_label; /* 暂停/播放 */
    lv_obj_t *back_btn;
    lv_obj_t *play_ctrl_bar; /* 播放控制栏 */
    lv_obj_t *path_label;    /* 路径标签 */
    lv_obj_t *time_label;    /* 时间 mm:ss/mm:ss */
    lv_timer_t *hide_timer;  /* 自动隐藏定时器 */

    /* 自动重播 */
    bool auto_replay;
    lv_obj_t *replay_switch; /* 重播开关 */
    lv_obj_t *replay_label;  /* 重播标签 */

    /* Seek  */
    bool seek_pending;
    bool seek_need_rewind;
    bool progress_syncing;
    bool progress_pressed;
    uint32_t seek_target_frame;
    uint32_t consecutive_decode_errors; /* 连续解码错误计数 */
    uint32_t *frame_index;
    uint32_t frame_index_count;
    uint32_t frame_index_cap;
} sd_video_ctx_t;

static sd_video_ctx_t g_video;
static sd_video_list_t g_video_list;

/* 列表UI */
static lv_obj_t *g_list_scr = NULL;
static lv_obj_t *g_list_obj = NULL;

/* 返回主菜单回调 */
static lv_event_cb_t g_back_to_menu_cb = NULL;

void lvgl_sd_video_set_back_to_menu_cb(lv_event_cb_t cb)
{
    g_back_to_menu_cb = cb;
}

/* 前向声明 */
static void lvgl_sd_video_show_list(void);
static void reset_hide_timer(sd_video_ctx_t *ctx);

static const uint8_t g_jpeg_std_dht[] = {
    0xFF, 0xC4, 0x01, 0xA2,
    0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D,
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22,
    0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33,
    0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34,
    0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55,
    0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76,
    0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5,
    0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
    0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1,
    0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,
    0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77,
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
    0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62,
    0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29,
    0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54,
    0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94,
    0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
    0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

static uint32_t rd_u32le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool has_avi_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    char ext[5];
    size_t len;

    if (dot == NULL)
    {
        return false;
    }

    dot++;
    if (*dot == '\0')
    {
        return false;
    }

    len = strlen(dot);
    if (len >= sizeof(ext))
    {
        return false;
    }

    for (size_t i = 0; i < len; i++)
    {
        ext[i] = (char)tolower((unsigned char)dot[i]);
    }
    ext[len] = '\0';

    return strcmp(ext, "avi") == 0;
}

/* 扫描SD卡中的.avi文件 */
int sd_video_scan_list(sd_video_list_t *list)
{
    struct fs_dir_t dir;
    struct fs_dirent ent;
    int ret;

    if (list == NULL)
    {
        return -EINVAL;
    }
    memset(list, 0, sizeof(*list));

    fs_dir_t_init(&dir);
    ret = fs_opendir(&dir, SD_VIDEO_SCAN_DIR);
    if (ret != 0)
    {
        LOG_ERR("opendir %s failed (%d)", SD_VIDEO_SCAN_DIR, ret);
        return ret;
    }

    while (list->count < SD_VIDEO_LIST_MAX)
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
        if (!has_avi_ext(ent.name))
        {
            continue;
        }

        snprintf(list->entries[list->count].name,
                 sizeof(list->entries[list->count].name),
                 "%s", ent.name);
        snprintf(list->entries[list->count].path,
                 sizeof(list->entries[list->count].path),
                 "%s/%s", SD_VIDEO_SCAN_DIR, ent.name);
        list->count++;
    }

    fs_closedir(&dir);
    LOG_INF("found %d AVI files", list->count);
    return 0;
}

static int read_exact(struct fs_file_t *file, void *buf, size_t len)
{
    ssize_t rd = fs_read(file, buf, len);
    if (rd < 0)
    {
        return (int)rd;
    }
    if ((size_t)rd != len)
    {
        return -EIO;
    }
    return 0;
}

static bool is_video_chunk_id(uint32_t id)
{
    char c0 = (char)(id & 0xFF);
    char c1 = (char)((id >> 8) & 0xFF);
    char c2 = (char)((id >> 16) & 0xFF);
    char c3 = (char)((id >> 24) & 0xFF);

    if (!isdigit((unsigned char)c0) || !isdigit((unsigned char)c1))
    {
        return false;
    }
    if (c2 != 'd')
    {
        return false;
    }
    return (c3 == 'c');
}

static bool is_list_chunk_id(uint32_t id)
{
    return (((id) & 0xFFU) == 'L') &&
           (((id >> 8) & 0xFFU) == 'I') &&
           (((id >> 16) & 0xFFU) == 'S') &&
           (((id >> 24) & 0xFFU) == 'T');
}

static bool find_jpeg_span(const uint8_t *buf, uint32_t size, uint32_t *off, uint32_t *len)
{
    uint32_t soi = UINT32_MAX;
    uint32_t eoi = UINT32_MAX;

    if (size < 4U)
    {
        return false;
    }

    for (uint32_t i = 0; i + 1U < size; i++)
    {
        if ((buf[i] == 0xFFU) && (buf[i + 1U] == 0xD8U))
        {
            soi = i;
            break;
        }
    }
    if (soi == UINT32_MAX)
    {
        return false;
    }

    for (uint32_t i = size - 1U; i > soi; i--)
    {
        if ((buf[i - 1U] == 0xFFU) && (buf[i] == 0xD9U))
        {
            eoi = i;
            break;
        }
    }
    if ((eoi == UINT32_MAX) || (eoi <= soi + 1U))
    {
        return false;
    }

    *off = soi;
    *len = eoi - soi + 1U;
    return true;
}

static bool jpeg_parse_tables(const uint8_t *buf, uint32_t len, bool *has_dht)
{
    uint32_t pos = 0U;
    bool saw_sos = false;
    *has_dht = false;

    if (len < 4U || buf[0] != 0xFFU || buf[1] != 0xD8U)
    {
        return false;
    }

    pos = 2U;
    while (pos + 1U < len)
    {
        while (pos < len && buf[pos] == 0xFFU)
        {
            pos++;
        }
        if (pos >= len)
        {
            break;
        }

        uint8_t marker = buf[pos++];
        if (marker == 0xDAU)
        {
            saw_sos = true;
            break;
        }
        if (marker == 0xD9U)
        {
            break;
        }
        if (marker == 0x01U || (marker >= 0xD0U && marker <= 0xD7U))
        {
            continue;
        }
        if (pos + 2U > len)
        {
            return false;
        }

        uint16_t seg_len = ((uint16_t)buf[pos] << 8) | buf[pos + 1U];
        pos += 2U;
        if (seg_len < 2U || pos + seg_len - 2U > len)
        {
            return false;
        }
        if (marker == 0xC4U)
        {
            *has_dht = true;
        }
        pos += (uint32_t)seg_len - 2U;
    }

    return saw_sos;
}

static bool inject_default_dht_if_needed(uint8_t *buf, uint32_t *len, uint32_t cap)
{
    bool has_dht;
    uint32_t old_len = *len;
    uint32_t dht_len = (uint32_t)sizeof(g_jpeg_std_dht);

    if (!jpeg_parse_tables(buf, old_len, &has_dht))
    {
        return false;
    }
    if (has_dht)
    {
        return true;
    }
    if (old_len + dht_len > cap || old_len < 2U)
    {
        return false;
    }

    memmove(buf + 2U + dht_len, buf + 2U, old_len - 2U);
    memcpy(buf + 2U, g_jpeg_std_dht, dht_len);
    *len = old_len + dht_len;
    return true;
}

static void video_close(sd_video_ctx_t *ctx)
{
    if (ctx->timer != NULL)
    {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    if (ctx->hide_timer != NULL)
    {
        lv_timer_del(ctx->hide_timer);
        ctx->hide_timer = NULL;
    }

    if (ctx->file_opened)
    {
        (void)fs_close(&ctx->file);
        ctx->file_opened = false;
    }

    if (ctx->jpeg_buf != NULL)
    {
        myfree(SRAMEX, ctx->jpeg_buf);
        ctx->jpeg_buf = NULL;
        ctx->jpeg_buf_cap = 0U;
    }

    if (ctx->rgb_buf != NULL)
    {
        myfree(SRAMEX, ctx->rgb_buf);
        ctx->rgb_buf = NULL;
    }
    if (ctx->frame_index != NULL)
    {
        myfree(SRAMEX, ctx->frame_index);
        ctx->frame_index = NULL;
        ctx->frame_index_count = 0U;
        ctx->frame_index_cap = 0U;
    }

    /* JPEG DMAֹͣ */
    jpeg_dma_stop();

    /* 清除UI元素引用 */
    ctx->progress_bar = NULL;
    ctx->pause_btn = NULL;
    ctx->pause_label = NULL;
    ctx->back_btn = NULL;
    ctx->play_ctrl_bar = NULL;
    ctx->path_label = NULL;
    ctx->hide_timer = NULL;
    ctx->img_obj = NULL;
    ctx->left_label = NULL;
    ctx->info_label = NULL;
    ctx->seek_pending = false;
    ctx->seek_need_rewind = false;
    ctx->progress_syncing = false;
    ctx->progress_pressed = false;
    ctx->seek_target_frame = 0U;
    ctx->consecutive_decode_errors = 0U;
}

static bool try_match_chunk_id(struct fs_file_t *file, uint32_t file_size, uint32_t hdr_off, uint32_t expect_id)
{
    uint8_t id_buf[4];

    if (hdr_off + 8U > file_size)
    {
        return false;
    }
    if (fs_seek(file, (off_t)hdr_off, FS_SEEK_SET) != 0)
    {
        return false;
    }
    if (read_exact(file, id_buf, sizeof(id_buf)) != 0)
    {
        return false;
    }
    return rd_u32le(id_buf) == expect_id;
}

typedef enum
{
    IDX1_MODE_RAW,
    IDX1_MODE_RAW_MINUS8,
    IDX1_MODE_MOVI_PLUS_RAW,
    IDX1_MODE_MOVI_PLUS_RAW_MINUS8,
    IDX1_MODE_MOVI_MARK_PLUS_RAW,
    IDX1_MODE_MOVI_MARK_PLUS_RAW_MINUS8,
    IDX1_MODE_COUNT
} idx1_off_mode_t;

static bool idx1_calc_hdr_off(sd_video_ctx_t *ctx, uint32_t raw_off, idx1_off_mode_t mode, uint32_t *out_hdr_off)
{
    uint32_t movi_mark = (ctx->movi_data_off >= 4U) ? (ctx->movi_data_off - 4U) : 0U;
    uint32_t hdr_off = 0U;

    switch (mode)
    {
    case IDX1_MODE_RAW:
        hdr_off = raw_off;
        break;
    case IDX1_MODE_RAW_MINUS8:
        if (raw_off < 8U)
        {
            return false;
        }
        hdr_off = raw_off - 8U;
        break;
    case IDX1_MODE_MOVI_PLUS_RAW:
        hdr_off = ctx->movi_data_off + raw_off;
        break;
    case IDX1_MODE_MOVI_PLUS_RAW_MINUS8:
        if (raw_off < 8U)
        {
            return false;
        }
        hdr_off = ctx->movi_data_off + raw_off - 8U;
        break;
    case IDX1_MODE_MOVI_MARK_PLUS_RAW:
        hdr_off = movi_mark + raw_off;
        break;
    case IDX1_MODE_MOVI_MARK_PLUS_RAW_MINUS8:
        if (raw_off < 8U)
        {
            return false;
        }
        hdr_off = movi_mark + raw_off - 8U;
        break;
    default:
        return false;
    }

    *out_hdr_off = hdr_off;
    return true;
}

static bool idx1_select_mode(sd_video_ctx_t *ctx, uint32_t file_size, const uint8_t *idx_buf,
                             uint32_t entry_count, idx1_off_mode_t *out_mode)
{
    int best_score = -1;
    uint32_t best_tested = 0U;
    idx1_off_mode_t best_mode = IDX1_MODE_RAW;

    for (idx1_off_mode_t mode = IDX1_MODE_RAW; mode < IDX1_MODE_COUNT; mode++)
    {
        int score = 0;
        uint32_t tested = 0U;

        for (uint32_t i = 0; i < entry_count && tested < 12U; i++)
        {
            const uint8_t *ent = idx_buf + i * 16U;
            uint32_t chunk_id = rd_u32le(&ent[0]);
            uint32_t raw_off = rd_u32le(&ent[8]);
            uint32_t hdr_off;

            if (!is_video_chunk_id(chunk_id))
            {
                continue;
            }
            if (!idx1_calc_hdr_off(ctx, raw_off, mode, &hdr_off))
            {
                continue;
            }

            tested++;
            if (try_match_chunk_id(&ctx->file, file_size, hdr_off, chunk_id))
            {
                score++;
            }
        }

        if (tested > 0U &&
            (score > best_score || (score == best_score && tested > best_tested)))
        {
            best_score = score;
            best_tested = tested;
            best_mode = mode;
        }
    }

    if (best_score <= 0)
    {
        return false;
    }

    *out_mode = best_mode;
    return true;
}

static int build_video_index(sd_video_ctx_t *ctx)
{
    struct fs_dirent st;
    uint32_t file_size;
    uint32_t off = 12U;
    uint32_t idx_data_off = 0U;
    uint32_t idx_chunk_size = 0U;
    uint8_t *idx_buf = NULL;
    uint32_t entry_count;
    uint32_t video_entry_count = 0U;
    idx1_off_mode_t off_mode;
    int ret;

    memset(&st, 0, sizeof(st));
    ret = fs_stat(ctx->path, &st);
    if (ret != 0)
    {
        return ret;
    }
    file_size = st.size;
    if (file_size < 12U)
    {
        return -EINVAL;
    }

    ctx->frame_index_count = 0U;

    while (off + 8U <= file_size)
    {
        uint8_t hdr[8];
        uint32_t id;
        uint32_t chunk_size;
        uint32_t data_off;
        uint32_t next_off;

        ret = fs_seek(&ctx->file, (off_t)off, FS_SEEK_SET);
        if (ret != 0)
        {
            return ret;
        }
        ret = read_exact(&ctx->file, hdr, sizeof(hdr));
        if (ret != 0)
        {
            return ret;
        }

        id = rd_u32le(&hdr[0]);
        chunk_size = rd_u32le(&hdr[4]);
        data_off = off + 8U;
        next_off = data_off + chunk_size + (chunk_size & 1U);
        if (next_off <= off || next_off > file_size)
        {
            return -EINVAL;
        }

        if (id == rd_u32le((const uint8_t *)"idx1"))
        {
            idx_data_off = data_off;
            idx_chunk_size = chunk_size;
            break;
        }

        off = next_off;
    }

    if (idx_chunk_size < 16U || idx_data_off == 0U)
    {
        return -ENOENT;
    }

    idx_buf = mymalloc(SRAMEX, idx_chunk_size);
    if (idx_buf == NULL)
    {
        return -ENOMEM;
    }

    ret = fs_seek(&ctx->file, (off_t)idx_data_off, FS_SEEK_SET);
    if (ret != 0)
    {
        myfree(SRAMEX, idx_buf);
        return ret;
    }
    ret = read_exact(&ctx->file, idx_buf, idx_chunk_size);
    if (ret != 0)
    {
        myfree(SRAMEX, idx_buf);
        return ret;
    }

    entry_count = idx_chunk_size / 16U;
    for (uint32_t i = 0; i < entry_count; i++)
    {
        const uint8_t *ent = idx_buf + i * 16U;
        if (is_video_chunk_id(rd_u32le(&ent[0])))
        {
            video_entry_count++;
        }
    }
    if (video_entry_count == 0U)
    {
        myfree(SRAMEX, idx_buf);
        return -ENOENT;
    }

    if (ctx->frame_index != NULL)
    {
        myfree(SRAMEX, ctx->frame_index);
        ctx->frame_index = NULL;
        ctx->frame_index_cap = 0U;
        ctx->frame_index_count = 0U;
    }

    ctx->frame_index = mymalloc(SRAMEX, video_entry_count * sizeof(uint32_t));
    if (ctx->frame_index == NULL)
    {
        myfree(SRAMEX, idx_buf);
        return -ENOMEM;
    }
    ctx->frame_index_cap = video_entry_count;

    if (!idx1_select_mode(ctx, file_size, idx_buf, entry_count, &off_mode))
    {
        myfree(SRAMEX, idx_buf);
        return -ENOENT;
    }

    for (uint32_t i = 0; i < entry_count; i++)
    {
        const uint8_t *ent = idx_buf + i * 16U;
        uint32_t chunk_id = rd_u32le(&ent[0]);
        uint32_t raw_off = rd_u32le(&ent[8]);
        uint32_t hdr_off;

        if (!is_video_chunk_id(chunk_id))
        {
            continue;
        }
        if (!idx1_calc_hdr_off(ctx, raw_off, off_mode, &hdr_off))
        {
            continue;
        }
        if (hdr_off + 8U > file_size)
        {
            continue;
        }
        ctx->frame_index[ctx->frame_index_count++] = hdr_off;
    }

    myfree(SRAMEX, idx_buf);
    if (ctx->frame_index_count == 0U)
    {
        return -ENOENT;
    }
    return 0;
}

#if 0  /* 保留备用：按文件名查找第一个avi */
static int find_first_avi(char *out_path, size_t out_path_len)
{
    struct fs_dir_t dir;
    struct fs_dirent ent;
    int ret;

    fs_dir_t_init(&dir);
    ret = fs_opendir(&dir, SD_VIDEO_SCAN_DIR);
    if (ret != 0)
    {
        return ret;
    }

    while (true)
    {
        memset(&ent, 0, sizeof(ent));
        ret = fs_readdir(&dir, &ent);
        if (ret != 0)
        {
            fs_closedir(&dir);
            return ret;
        }
        if (ent.name[0] == '\0')
        {
            fs_closedir(&dir);
            return -ENOENT;
        }
        if (ent.type != FS_DIR_ENTRY_FILE)
        {
            continue;
        }
        if (!has_avi_ext(ent.name))
        {
            continue;
        }

        (void)snprintf(out_path, out_path_len, "%s/%s", SD_VIDEO_SCAN_DIR, ent.name);
        fs_closedir(&dir);
        return 0;
    }
}
#endif /* find_first_avi */

static int parse_avih_in_list(sd_video_ctx_t *ctx, uint32_t list_data_off, uint32_t list_data_len)
{
    uint32_t off = list_data_off;

    while (off + 8U <= list_data_off + list_data_len)
    {
        uint8_t hdr[8];
        uint32_t chunk_size;
        uint32_t chunk_data_off;
        uint32_t next_off;
        int ret;

        ret = fs_seek(&ctx->file, (off_t)off, FS_SEEK_SET);
        if (ret != 0)
        {
            return ret;
        }

        ret = read_exact(&ctx->file, hdr, sizeof(hdr));
        if (ret != 0)
        {
            return ret;
        }

        chunk_size = rd_u32le(&hdr[4]);
        chunk_data_off = off + 8U;
        next_off = chunk_data_off + chunk_size + (chunk_size & 1U);
        if (next_off < off)
        {
            return -EINVAL;
        }
        if (next_off > list_data_off + list_data_len)
        {
            break;
        }

        if ((hdr[0] == 'a') && (hdr[1] == 'v') && (hdr[2] == 'i') && (hdr[3] == 'h') && (chunk_size >= 40U))
        {
            uint8_t avih[40];
            ret = fs_seek(&ctx->file, (off_t)chunk_data_off, FS_SEEK_SET);
            if (ret != 0)
            {
                return ret;
            }
            ret = read_exact(&ctx->file, avih, sizeof(avih));
            if (ret != 0)
            {
                return ret;
            }

            ctx->us_per_frame = rd_u32le(&avih[0]);
            ctx->total_frames = rd_u32le(&avih[16]);
            ctx->frame_w = (uint16_t)rd_u32le(&avih[32]);
            ctx->frame_h = (uint16_t)rd_u32le(&avih[36]);
            return 0;
        }

        off = next_off;
    }

    return -ENOENT;
}

static int parse_avi_header(sd_video_ctx_t *ctx)
{
    struct fs_dirent st;
    uint8_t riff[12];
    uint32_t file_size;
    uint32_t off = 12U;
    bool got_avih = false;
    bool got_movi = false;
    int ret;

    memset(&st, 0, sizeof(st));
    ret = fs_stat(ctx->path, &st);
    if (ret != 0)
    {
        return ret;
    }
    if ((st.type != FS_DIR_ENTRY_FILE) || (st.size < 12U))
    {
        return -EINVAL;
    }
    file_size = st.size;

    ret = fs_seek(&ctx->file, 0, FS_SEEK_SET);
    if (ret != 0)
    {
        return ret;
    }
    ret = read_exact(&ctx->file, riff, sizeof(riff));
    if (ret != 0)
    {
        return ret;
    }
    if ((memcmp(&riff[0], "RIFF", 4) != 0) || (memcmp(&riff[8], "AVI ", 4) != 0))
    {
        return -EINVAL;
    }

    while (off + 8U <= file_size)
    {
        uint8_t hdr[8];
        uint32_t chunk_size;
        uint32_t chunk_data_off;
        uint32_t next_off;

        ret = fs_seek(&ctx->file, (off_t)off, FS_SEEK_SET);
        if (ret != 0)
        {
            return ret;
        }
        ret = read_exact(&ctx->file, hdr, sizeof(hdr));
        if (ret != 0)
        {
            return ret;
        }

        chunk_size = rd_u32le(&hdr[4]);
        chunk_data_off = off + 8U;
        next_off = chunk_data_off + chunk_size + (chunk_size & 1U);
        if (next_off < off || next_off > file_size)
        {
            return -EINVAL;
        }

        if ((hdr[0] == 'L') && (hdr[1] == 'I') && (hdr[2] == 'S') && (hdr[3] == 'T') && (chunk_size >= 4U))
        {
            uint8_t list_type[4];

            ret = fs_seek(&ctx->file, (off_t)chunk_data_off, FS_SEEK_SET);
            if (ret != 0)
            {
                return ret;
            }
            ret = read_exact(&ctx->file, list_type, sizeof(list_type));
            if (ret != 0)
            {
                return ret;
            }

            if ((memcmp(list_type, "hdrl", 4) == 0) && !got_avih)
            {
                ret = parse_avih_in_list(ctx, chunk_data_off + 4U, chunk_size - 4U);
                if (ret == 0)
                {
                    got_avih = true;
                }
            }
            else if (memcmp(list_type, "movi", 4) == 0)
            {
                ctx->movi_data_off = chunk_data_off + 4U;
                ctx->movi_data_end = chunk_data_off + chunk_size;
                ctx->next_chunk_off = ctx->movi_data_off;
                got_movi = true;
            }
        }

        if (got_avih && got_movi)
        {
            break;
        }
        off = next_off;
    }

    if (!got_avih || !got_movi || (ctx->frame_w == 0U) || (ctx->frame_h == 0U))
    {
        return -EINVAL;
    }
    if (ctx->us_per_frame == 0U)
    {
        ctx->us_per_frame = 33333U;
    }

    return 0;
}

static int ensure_jpeg_buf(sd_video_ctx_t *ctx, uint32_t size)
{
    if (size > SD_VIDEO_MAX_FRAME_SIZE)
    {
        return -EFBIG;
    }
    if (ctx->jpeg_buf_cap >= size)
    {
        return 0;
    }

    if (ctx->jpeg_buf != NULL)
    {
        myfree(SRAMEX, ctx->jpeg_buf);
        ctx->jpeg_buf = NULL;
        ctx->jpeg_buf_cap = 0U;
    }

    ctx->jpeg_buf = mymalloc(SRAMEX, size);
    if (ctx->jpeg_buf == NULL)
    {
        return -ENOMEM;
    }
    ctx->jpeg_buf_cap = size;
    return 0;
}

static bool video_locate_next_video_chunk(sd_video_ctx_t *ctx, uint32_t *out_data_off, uint32_t *out_chunk_size)
{
    for (int i = 0; i < 512; i++)
    {
        uint8_t hdr[8];
        uint8_t list_type[4];
        uint32_t id;
        uint32_t chunk_size;
        uint32_t boundary_end;
        uint32_t data_off;
        uint32_t next_off;
        int ret;

        boundary_end = (ctx->rec_list_end != 0U) ? ctx->rec_list_end : ctx->movi_data_end;
        if (ctx->next_chunk_off + 8U > boundary_end)
        {
            if (ctx->rec_list_end != 0U)
            {
                ctx->next_chunk_off = ctx->rec_list_end;
                ctx->rec_list_end = 0U;
                continue;
            }
            ctx->next_chunk_off = ctx->movi_data_off;
        }

        ret = fs_seek(&ctx->file, (off_t)ctx->next_chunk_off, FS_SEEK_SET);
        if (ret != 0)
        {
            return false;
        }
        ret = read_exact(&ctx->file, hdr, sizeof(hdr));
        if (ret != 0)
        {
            return false;
        }

        id = rd_u32le(&hdr[0]);
        chunk_size = rd_u32le(&hdr[4]);
        data_off = ctx->next_chunk_off + 8U;
        next_off = data_off + chunk_size + (chunk_size & 1U);
        if (next_off <= ctx->next_chunk_off)
        {
            return false;
        }
        ctx->next_chunk_off = next_off;

        if (next_off > boundary_end)
        {
            if (ctx->rec_list_end != 0U)
            {
                ctx->next_chunk_off = ctx->rec_list_end;
                ctx->rec_list_end = 0U;
                continue;
            }
            ctx->next_chunk_off = ctx->movi_data_off;
            continue;
        }

        if (is_list_chunk_id(id) && (chunk_size >= 4U))
        {
            ret = fs_seek(&ctx->file, (off_t)data_off, FS_SEEK_SET);
            if (ret != 0)
            {
                return false;
            }
            ret = read_exact(&ctx->file, list_type, sizeof(list_type));
            if (ret != 0)
            {
                return false;
            }

            if (memcmp(list_type, "rec ", 4) == 0)
            {
                ctx->rec_list_end = next_off;
                ctx->next_chunk_off = data_off + 4U;
            }
            continue;
        }

        if (!is_video_chunk_id(id))
        {
            continue;
        }
        if (chunk_size == 0U)
        {
            continue;
        }

        if (out_data_off != NULL)
        {
            *out_data_off = data_off;
        }
        if (out_chunk_size != NULL)
        {
            *out_chunk_size = chunk_size;
        }
        return true;
    }

    return false;
}

static bool read_next_video_frame(sd_video_ctx_t *ctx, uint16_t *out_w, uint16_t *out_h)
{
    for (int i = 0; i < 512; i++)
    {
        uint32_t data_off;
        uint32_t chunk_size;
        uint32_t jpeg_off;
        uint32_t jpeg_len;
        int ret;

        if (!video_locate_next_video_chunk(ctx, &data_off, &chunk_size))
        {
            return false;
        }

        ret = ensure_jpeg_buf(ctx, chunk_size + (uint32_t)sizeof(g_jpeg_std_dht));
        if (ret != 0)
        {
            LOG_ERR("frame buf err %d size=%u", ret, (unsigned int)chunk_size);
            return false;
        }

        ret = fs_seek(&ctx->file, (off_t)data_off, FS_SEEK_SET);
        if (ret != 0)
        {
            return false;
        }
        ret = read_exact(&ctx->file, ctx->jpeg_buf, chunk_size);
        if (ret != 0)
        {
            return false;
        }

        if (!find_jpeg_span(ctx->jpeg_buf, chunk_size, &jpeg_off, &jpeg_len))
        {
            continue;
        }
        if (jpeg_off != 0U)
        {
            memmove(ctx->jpeg_buf, ctx->jpeg_buf + jpeg_off, jpeg_len);
        }
        if (!inject_default_dht_if_needed(ctx->jpeg_buf, &jpeg_len, ctx->jpeg_buf_cap))
        {
            continue;
        }

        (void)sys_cache_data_flush_range(ctx->jpeg_buf, jpeg_len);
        if (jpeg_decode_to_rgb565(ctx->jpeg_buf, jpeg_len, ctx->rgb_buf,
                                  ctx->frame_w, ctx->frame_h, out_w, out_h) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool skip_next_video_frame(sd_video_ctx_t *ctx)
{
    return video_locate_next_video_chunk(ctx, NULL, NULL);
}

/*    mm:ss/mm:ss */
static void video_time_update(sd_video_ctx_t *ctx, uint32_t frame)
{
    if (ctx == NULL || ctx->time_label == NULL || ctx->us_per_frame == 0U)
    {
        return;
    }

    /*  =    / 1,000,000 */
    uint64_t cur_us = (uint64_t)frame * ctx->us_per_frame;
    uint32_t cur_sec = (uint32_t)(cur_us / 1000000U);
    uint32_t cur_min = cur_sec / 60U;
    cur_sec = cur_sec % 60U;

    /*  =    / 1,000,000 */
    uint64_t total_us = (uint64_t)ctx->total_frames * ctx->us_per_frame;
    uint32_t total_sec = (uint32_t)(total_us / 1000000U);
    uint32_t total_min = total_sec / 60U;
    total_sec = total_sec % 60U;

    lv_label_set_text_fmt(ctx->time_label, "%02u:%02u/%02u:%02u",
                          cur_min, cur_sec, total_min, total_sec);
}

static void video_progress_set(sd_video_ctx_t *ctx, int32_t progress, uint32_t frame)
{
    if (ctx == NULL || ctx->progress_bar == NULL)
    {
        return;
    }
    if (ctx->progress_pressed)
    {
        return;
    }

    if (progress < 0)
    {
        progress = 0;
    }
    else if (progress > 100)
    {
        progress = 100;
    }

    ctx->progress_syncing = true;
    lv_slider_set_value(ctx->progress_bar, progress, LV_ANIM_OFF);
    ctx->progress_syncing = false;

    /*  progress  */
    video_time_update(ctx, frame);
}

static void video_frame_timer_cb(lv_timer_t *timer)
{
    sd_video_ctx_t *ctx = (sd_video_ctx_t *)lv_timer_get_user_data(timer);
    uint16_t w = 0U;
    uint16_t h = 0U;

    if (ctx == NULL || ctx->rgb_buf == NULL || ctx->img_obj == NULL)
    {
        return;
    }

    /*  seek  */
    if (ctx->seek_pending)
    {
        if (ctx->frame_index_count > 0U)
        {
            uint32_t idx = ctx->seek_target_frame;
            if (idx >= ctx->frame_index_count)
            {
                idx = ctx->frame_index_count - 1U;
            }
            ctx->next_chunk_off = ctx->frame_index[idx];
            ctx->rec_list_end = 0U;
            ctx->current_frame = idx;
            ctx->seek_need_rewind = false;
            ctx->seek_pending = false;

            if (ctx->progress_bar != NULL && ctx->total_frames > 0U)
            {
                int32_t progress = (int32_t)((int64_t)idx * 100 / ctx->total_frames);
                video_progress_set(ctx, progress, idx);
            }
            LOG_INF("seek indexed to frame %u", (unsigned int)idx);
            return;
        }

        uint32_t frames_to_skip = 8U; /* 8 */
        if (ctx->seek_need_rewind)
        {
            ctx->next_chunk_off = ctx->movi_data_off;
            ctx->rec_list_end = 0U;
            ctx->current_frame = 0U;
            ctx->seek_need_rewind = false;
        }

        if (ctx->seek_target_frame > ctx->current_frame)
        {
            uint32_t remaining = ctx->seek_target_frame - ctx->current_frame;
            if (frames_to_skip > remaining)
            {
                frames_to_skip = remaining;
            }
        }
        for (uint32_t i = 0; i < frames_to_skip; i++)
        {
            if (!skip_next_video_frame(ctx))
            {
                /* seek  */
                ctx->next_chunk_off = ctx->movi_data_off;
                ctx->rec_list_end = 0U;
                ctx->current_frame = 0U;
                ctx->seek_pending = false;
                LOG_WRN("seek failed, reset to start");
                break;
            }
            ctx->current_frame++;
        }

        if (ctx->current_frame >= ctx->seek_target_frame)
        {
            ctx->seek_pending = false;
            LOG_INF("seek done at frame %u",
                    (unsigned int)ctx->current_frame);
        }

        /* 同步进度条 */
        if (ctx->progress_bar != NULL && ctx->total_frames > 0U)
        {
            int32_t progress = (int32_t)((int64_t)ctx->seek_target_frame * 100 / ctx->total_frames);
            video_progress_set(ctx, progress, ctx->seek_target_frame);
        }
        return; /* seek  seek  */
    }

    /* 暂停状态不处理帧 */
    if (ctx->state == VIDEO_STATE_PAUSED)
    {
        return;
    }

    if (!read_next_video_frame(ctx, &w, &h))
    {
        ctx->consecutive_decode_errors++;
        /* 连续解码错误过多则退出 */
        if (ctx->consecutive_decode_errors >= 10U)
        {
            LOG_WRN("playback finished (too many errors)");
            lvgl_sd_video_show_list();
            return;
        }
        /* 跳过当前帧 */
        return;
    }
    ctx->consecutive_decode_errors = 0U;

    if (w == 0U || h == 0U)
    {
        return;
    }

    ctx->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    ctx->img_dsc.header.w = w;
    ctx->img_dsc.header.h = h;
    ctx->img_dsc.data_size = (uint32_t)w * h * sizeof(uint16_t);
    ctx->img_dsc.data = (const uint8_t *)ctx->rgb_buf;

    (void)sys_cache_data_invd_range(ctx->rgb_buf, ctx->img_dsc.data_size);
    lv_image_set_src(ctx->img_obj, &ctx->img_dsc);
    lv_obj_align(ctx->img_obj, LV_ALIGN_CENTER, 0, 0);

    /* 更新帧计数 */
    ctx->current_frame++;
    if (ctx->progress_bar != NULL && ctx->total_frames > 0U)
    {
        int32_t progress = (int32_t)((int64_t)ctx->current_frame * 100 / ctx->total_frames);
        video_progress_set(ctx, progress, ctx->current_frame);
    }

    /* 检查是否播放完毕 */
    if (ctx->total_frames > 0U && ctx->current_frame >= ctx->total_frames)
    {
        LOG_INF("playback finished (end of video)");
        if (ctx->auto_replay)
        {
            /* 自动重播 */
            LOG_INF("auto replay");
            ctx->current_frame = 0U;
            ctx->seek_need_rewind = true;
            ctx->seek_target_frame = 0U;
            ctx->seek_pending = true;
            if (ctx->progress_bar != NULL)
            {
                lv_slider_set_value(ctx->progress_bar, 0, LV_ANIM_OFF);
            }
            video_time_update(ctx, 0U);
        }
        else
        {
            lvgl_sd_video_show_list();
        }
    }
}

/* ========== 播放控制回调 ========== */

/* 暂停/播放 */
static void on_pause_btn_click(lv_event_t *e)
{
    sd_video_ctx_t *ctx = &g_video;

    if (ctx->state == VIDEO_STATE_PLAYING)
    {
        ctx->state = VIDEO_STATE_PAUSED;
        if (ctx->pause_label != NULL)
            lv_label_set_text(ctx->pause_label, LV_SYMBOL_PLAY);
        LOG_INF("paused");
    }
    else if (ctx->state == VIDEO_STATE_PAUSED)
    {
        ctx->state = VIDEO_STATE_PLAYING;
        if (ctx->pause_label != NULL)
            lv_label_set_text(ctx->pause_label, LV_SYMBOL_PAUSE);
        LOG_INF("resumed");
    }

    /* 重置隐藏定时器 */
    reset_hide_timer(ctx);
}

/* 视频播放页面返回 - 返回视频选择列表 */
static void on_back_btn_click(lv_event_t *e)
{
    LOG_INF("back to video list");
    lvgl_sd_video_show_list();
}

/* 进度条拖动 seek 处理 */
static void on_progress_bar_value_changed(lv_event_t *e)
{
    sd_video_ctx_t *ctx = &g_video;
    lv_obj_t *slider = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED)
    {
        ctx->progress_pressed = true;
        return;
    }

    /* 拖动中实时更新时间标签 */
    if (code == LV_EVENT_VALUE_CHANGED && ctx->progress_pressed)
    {
        if (ctx->total_frames > 0U)
        {
            int32_t val = lv_slider_get_value(slider);
            uint32_t frame = (uint32_t)((int64_t)val * ctx->total_frames / 100);
            video_time_update(ctx, frame);
        }
        return;
    }

    if (code != LV_EVENT_RELEASED)
    {
        return;
    }
    ctx->progress_pressed = false;
    if (ctx->progress_syncing)
    {
        return;
    }

    if (ctx->total_frames == 0U)
    {
        return;
    }

    int32_t val = lv_slider_get_value(slider);
    uint32_t target_frame = (uint32_t)((int64_t)val * ctx->total_frames / 100);

    if (target_frame >= ctx->total_frames)
    {
        target_frame = ctx->total_frames - 1U;
    }

    /* 判断是否需要从头回退 */
    ctx->seek_need_rewind = (target_frame < ctx->current_frame);

    ctx->seek_target_frame = target_frame;
    ctx->seek_pending = true;

    LOG_INF("seek requested frame %u/%u (current=%u)",
            (unsigned int)target_frame,
            (unsigned int)ctx->total_frames,
            (unsigned int)ctx->current_frame);

    /* 重置隐藏定时器 */
    reset_hide_timer(ctx);
}

/* 重播开关回调 */
static void on_replay_switch_changed(lv_event_t *e)
{
    sd_video_ctx_t *ctx = &g_video;
    lv_obj_t *sw = lv_event_get_target(e);
    ctx->auto_replay = lv_obj_has_state(sw, LV_STATE_CHECKED);
    LOG_INF("auto replay: %s", ctx->auto_replay ? "ON" : "OFF");

    /* 重置隐藏定时器 */
    reset_hide_timer(ctx);
}

/* 点击控制栏不冒泡到 on_screen_click toggle */
static void on_control_bar_click_block(lv_event_t *e)
{
    /* 阻止事件冒泡 */
    lv_event_stop_bubbling(e);
}

/* ========== 创建播放控制UI ========== */
static void create_play_controls(sd_video_ctx_t *ctx, lv_obj_t *parent)
{
    /* 创建控制栏容器 */
    ctx->play_ctrl_bar = lv_obj_create(parent);
    lv_obj_set_size(ctx->play_ctrl_bar, LV_PCT(100), 48);
    lv_obj_align(ctx->play_ctrl_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->play_ctrl_bar, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(ctx->play_ctrl_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(ctx->play_ctrl_bar, 0, 0);
    lv_obj_set_style_pad_all(ctx->play_ctrl_bar, 4, 0);
    lv_obj_set_style_radius(ctx->play_ctrl_bar, 0, 0);
    lv_obj_clear_flag(ctx->play_ctrl_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* 返回按钮 */
    ctx->back_btn = lv_button_create(ctx->play_ctrl_bar);
    lv_obj_set_size(ctx->back_btn, 40, 40);
    lv_obj_align(ctx->back_btn, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_color(ctx->back_btn, lv_color_hex(0x404040), 0);
    lv_obj_add_event_cb(ctx->back_btn, on_back_btn_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(ctx->back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    /* / */
    ctx->pause_btn = lv_button_create(ctx->play_ctrl_bar);
    lv_obj_set_size(ctx->pause_btn, 40, 40);
    lv_obj_align(ctx->pause_btn, LV_ALIGN_LEFT_MID, 50, 0);
    lv_obj_set_style_bg_color(ctx->pause_btn, lv_color_hex(0x404040), 0);
    lv_obj_add_event_cb(ctx->pause_btn, on_pause_btn_click, LV_EVENT_CLICKED, NULL);

    ctx->pause_label = lv_label_create(ctx->pause_btn);
    lv_label_set_text(ctx->pause_label, LV_SYMBOL_PAUSE);
    lv_obj_center(ctx->pause_label);

    /* 进度条 */
    ctx->progress_bar = lv_slider_create(ctx->play_ctrl_bar);
    lv_obj_set_size(ctx->progress_bar, LV_PCT(50), 20);
    lv_obj_align(ctx->progress_bar, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(ctx->progress_bar, 0, 100);
    lv_slider_set_value(ctx->progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ctx->progress_bar, lv_color_hex(0x404040), 0);
    lv_obj_set_style_bg_opa(ctx->progress_bar, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(ctx->progress_bar, on_progress_bar_value_changed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ctx->progress_bar, on_progress_bar_value_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ctx->progress_bar, on_progress_bar_value_changed, LV_EVENT_RELEASED, NULL);

    /*    mm:ss/mm:ss */
    ctx->time_label = lv_label_create(ctx->play_ctrl_bar);
    lv_label_set_text(ctx->time_label, "00:00/00:00");
    lv_obj_set_style_text_color(ctx->time_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(ctx->time_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(ctx->time_label, ctx->progress_bar, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    /* 重播开关 */
    ctx->replay_switch = lv_switch_create(ctx->play_ctrl_bar);
    lv_obj_set_size(ctx->replay_switch, 40, 24);
    lv_obj_align(ctx->replay_switch, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(ctx->replay_switch, on_replay_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (ctx->auto_replay)
    {
        lv_obj_add_state(ctx->replay_switch, LV_STATE_CHECKED);
    }

    ctx->replay_label = lv_label_create(ctx->play_ctrl_bar);
    lv_label_set_text(ctx->replay_label, "R");
    lv_obj_set_style_text_color(ctx->replay_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(ctx->replay_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(ctx->replay_label, ctx->replay_switch, LV_ALIGN_OUT_LEFT_MID, -4, 0);

    /* 阻止 on_screen_click toggle */
    lv_obj_add_event_cb(ctx->play_ctrl_bar, on_control_bar_click_block, LV_EVENT_CLICKED, NULL);
}

/* 隐藏控制栏定时器回调 */
static void hide_controls_timer_cb(lv_timer_t *timer)
{
    sd_video_ctx_t *ctx = (sd_video_ctx_t *)lv_timer_get_user_data(timer);

    if (ctx == NULL || ctx->play_ctrl_bar == NULL)
    {
        return;
    }

    /* 进度条拖动中不隐藏 */
    if (ctx->progress_pressed)
    {
        ctx->hide_timer = lv_timer_create(hide_controls_timer_cb, 3000, ctx);
        if (ctx->hide_timer != NULL)
        {
            lv_timer_set_repeat_count(ctx->hide_timer, 1);
        }
        return;
    }

    /* 隐藏控制栏 */
    lv_obj_add_flag(ctx->play_ctrl_bar, LV_OBJ_FLAG_HIDDEN);
    if (ctx->path_label != NULL)
    {
        lv_obj_add_flag(ctx->path_label, LV_OBJ_FLAG_HIDDEN);
    }

    /* LVGL 定时器自删除 */
    ctx->hide_timer = NULL;
}

/* 重置隐藏定时器 */
static void reset_hide_timer(sd_video_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    /* 显示控制栏 */
    if (ctx->play_ctrl_bar != NULL)
    {
        lv_obj_clear_flag(ctx->play_ctrl_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->path_label != NULL)
    {
        lv_obj_clear_flag(ctx->path_label, LV_OBJ_FLAG_HIDDEN);
    }

    /* 取消旧定时器 */
    if (ctx->hide_timer != NULL)
    {
        lv_timer_del(ctx->hide_timer);
        ctx->hide_timer = NULL;
    }

    /* 3秒后自动隐藏 */
    ctx->hide_timer = lv_timer_create(hide_controls_timer_cb, 3000, ctx);
    if (ctx->hide_timer != NULL)
    {
        lv_timer_set_repeat_count(ctx->hide_timer, 1); /* 单次触发 */
    }
}

/* 点击屏幕切换控制栏显示/隐藏 */
static void on_screen_click(lv_event_t *e)
{
    sd_video_ctx_t *ctx = &g_video;

    if (ctx->play_ctrl_bar == NULL)
    {
        return;
    }

    /* 切换显示/隐藏 */
    if (lv_obj_has_flag(ctx->play_ctrl_bar, LV_OBJ_FLAG_HIDDEN))
    {
        /*    */
        lv_obj_clear_flag(ctx->play_ctrl_bar, LV_OBJ_FLAG_HIDDEN);
        if (ctx->path_label != NULL)
        {
            lv_obj_clear_flag(ctx->path_label, LV_OBJ_FLAG_HIDDEN);
        }
        reset_hide_timer(ctx);
    }
    else
    {
        /*    */
        lv_obj_add_flag(ctx->play_ctrl_bar, LV_OBJ_FLAG_HIDDEN);
        if (ctx->path_label != NULL)
        {
            lv_obj_add_flag(ctx->path_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (ctx->hide_timer != NULL)
        {
            lv_timer_del(ctx->hide_timer);
            ctx->hide_timer = NULL;
        }
    }
}

/* ========== 视频播放 ========== */
static int video_play(const char *path)
{
    lv_obj_t *scr;
    uint32_t frame_ms;
    int ret;

    /* 关闭之前的视频 */
    video_close(&g_video);
    memset(&g_video, 0, sizeof(g_video));
    fs_file_t_init(&g_video.file);

    /* 复制路径 */
    snprintf(g_video.path, sizeof(g_video.path), "%s", path);

    ret = fs_open(&g_video.file, g_video.path, FS_O_READ);
    if (ret != 0)
    {
        LOG_ERR("open failed %s (%d)", g_video.path, ret);
        return ret;
    }
    g_video.file_opened = true;

    ret = parse_avi_header(&g_video);
    if (ret != 0)
    {
        LOG_ERR("bad AVI %s (%d)", g_video.path, ret);
        video_close(&g_video);
        return ret;
    }

    g_video.rgb_buf = mymalloc(SRAMEX, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t) + 65536U);
    if (g_video.rgb_buf == NULL)
    {
        video_close(&g_video);
        return -ENOMEM;
    }
    memset(g_video.rgb_buf, 0, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t));
    (void)sys_cache_data_flush_range(g_video.rgb_buf, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t));

    /* 创建播放屏幕 */
    scr = lv_obj_create(NULL);
    lv_screen_load(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    g_video.img_obj = lv_image_create(scr);
    lv_obj_align(g_video.img_obj, LV_ALIGN_CENTER, 0, -20);
    /* 设置图片对象背景 */
    lv_obj_set_style_bg_color(g_video.img_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_video.img_obj, LV_OPA_COVER, 0);

    /* 路径标签 显示在 img_obj 上方 */
    lv_obj_t *path_label = lv_label_create(scr);
    lv_label_set_text_fmt(path_label, "%s", path);
    lv_obj_set_style_text_color(path_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(path_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(path_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(path_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(path_label, 6, 0);
    lv_obj_set_width(path_label, LV_PCT(100));
    lv_obj_align(path_label, LV_ALIGN_TOP_MID, 0, 0);
    lv_label_set_long_mode(path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    g_video.path_label = path_label;

    g_video.left_label = lv_label_create(scr);
    lv_label_set_text(g_video.left_label, "Z\nE\nP\nH\nY\nR");
    lv_obj_set_width(g_video.left_label, 24);
    lv_label_set_long_mode(g_video.left_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(g_video.left_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_video.left_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_video.left_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_video.left_label, LV_ALIGN_LEFT_MID, 4, 0);

    g_video.info_label = lv_label_create(scr);
    lv_label_set_text(g_video.info_label, "L\nV\nG\nL\n\nB\ny\n\nQ\nY\nc\na\nt");
    lv_obj_set_width(g_video.info_label, 24);
    lv_label_set_long_mode(g_video.info_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(g_video.info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_video.info_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_video.info_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_video.info_label, LV_ALIGN_RIGHT_MID, -4, 0);

    /* 创建播放控制UI */
    create_play_controls(&g_video, scr);

    /* 点击屏幕切换控制栏 */
    lv_obj_add_event_cb(scr, on_screen_click, LV_EVENT_CLICKED, NULL);

    /* 初始化播放状态 */
    g_video.state = VIDEO_STATE_PLAYING;
    g_video.current_frame = 0U;
    g_video.seek_pending = false;
    g_video.seek_need_rewind = false;
    g_video.progress_syncing = false;
    g_video.progress_pressed = false;
    g_video.seek_target_frame = 0U;
    g_video.consecutive_decode_errors = 0U;
    g_video.hide_timer = NULL;
    g_video.auto_replay = false;
    g_video.replay_switch = NULL;
    g_video.replay_label = NULL;
    g_video.frame_index = NULL;
    g_video.frame_index_count = 0U;
    g_video.frame_index_cap = 0U;

    ret = build_video_index(&g_video);
    if (ret == 0)
    {
        LOG_INF("idx1 loaded, frames=%u", (unsigned int)g_video.frame_index_count);
    }
    else
    {
        LOG_WRN("idx1 unavailable (%d), fallback slow seek", ret);
    }

    frame_ms = g_video.us_per_frame / 1000U;
    if (frame_ms == 0U)
    {
        frame_ms = 1U;
    }
    g_video.timer = lv_timer_create(video_frame_timer_cb, frame_ms, &g_video);
    if (g_video.timer == NULL)
    {
        video_close(&g_video);
        return -ENOMEM;
    }
    /*  LVGL
     *  lv_scr_load  */
    lv_timer_ready(g_video.timer);

    /* 3 */
    reset_hide_timer(&g_video);

    LOG_INF("playing %s (%ux%u, %u frames, %lu us/frame)",
            g_video.path,
            (unsigned int)g_video.frame_w,
            (unsigned int)g_video.frame_h,
            (unsigned int)g_video.total_frames,
            (unsigned long)g_video.us_per_frame);
    return 0;
}

/* ========== 列表项点击 ========== */
static void on_list_item_click(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);

    if (idx < 0 || idx >= g_video_list.count)
    {
        return;
    }

    LOG_INF("selected [%d] %s", idx, g_video_list.entries[idx].name);

    /* 开始播放 - video_play 内部会 lv_scr_load 新 screen */
    video_play(g_video_list.entries[idx].path);
}

/* ========== 显示视频列表 ========== */
static void lvgl_sd_video_show_list(void)
{
    lv_obj_t *title;

    /* 关闭当前视频 */
    video_close(&g_video);

    /* 删除旧列表 screen 释放内存 */
    if (g_list_scr != NULL)
    {
        lv_obj_del(g_list_scr);
        g_list_scr = NULL;
        g_list_obj = NULL;
    }

    /* 创建列表屏幕 */
    g_list_scr = lv_obj_create(NULL);
    lv_screen_load(g_list_scr);
    lv_obj_set_style_bg_color(g_list_scr, lv_color_hex(0x101020), 0);
    lv_obj_set_style_pad_all(g_list_scr, 0, 0);

    /* SD卡背景图片 */
    const lv_image_dsc_t *bg_img = lvgl_sd_pic_get_image();
    if (bg_img != NULL && bg_img->data != NULL)
    {
        lv_obj_t *bg = lv_image_create(g_list_scr);
        lv_image_set_src(bg, bg_img);
        lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_img_opa(bg, LV_OPA_30, 0); /* 半透明 */
                                                    /* 置于最底层 */
        lv_obj_move_to_index(bg, 0);
    }

    /* 标题 */
    title = lv_label_create(g_list_scr);
    lv_label_set_text(title, "Video List");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 创建列表 */
    g_list_obj = lv_list_create(g_list_scr);
    lv_obj_set_size(g_list_obj, LV_PCT(95), LV_PCT(85));
    lv_obj_align(g_list_obj, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(g_list_obj, lv_color_hex(0x181830), 0);
    lv_obj_set_style_bg_opa(g_list_obj, LV_OPA_70, 0); /* 半透明 */
    lv_obj_set_style_border_width(g_list_obj, 0, 0);
    lv_obj_set_style_pad_all(g_list_obj, 4, 0);

    /* 添加列表项 */
    for (int i = 0; i < g_video_list.count; i++)
    {
        lv_obj_t *btn = lv_list_add_btn(g_list_obj, LV_SYMBOL_VIDEO,
                                        g_video_list.entries[i].name);
        /* 设置按钮样式 */
        lv_obj_set_height(btn, 56);
        lv_obj_set_style_pad_top(btn, 12, 0);
        lv_obj_set_style_pad_bottom(btn, 12, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, on_list_item_click, LV_EVENT_CLICKED, NULL);
    }

    /* 空列表提示 */
    if (g_video_list.count == 0)
    {
        lv_obj_t *empty_label = lv_label_create(g_list_scr);
        lv_label_set_text(empty_label, "No video files found\nin /SD:/VIDEO");
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x808080), 0);
        lv_obj_align(empty_label, LV_ALIGN_CENTER, 0, 0);
    }

    /* 返回按钮 */
    lv_obj_t *back_btn = lv_button_create(g_list_scr);
    lv_obj_set_size(back_btn, 80, 35);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    if (g_back_to_menu_cb)
    {
        lv_obj_add_event_cb(back_btn, g_back_to_menu_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_center(back_lbl);

    LOG_INF("showing list with %d videos", g_video_list.count);
}

/* ========== 启动入口 ========== */
int lvgl_sd_video_start(void)
{
    int ret;

    /* 扫描视频文件 */
    ret = sd_video_scan_list(&g_video_list);
    if (ret != 0 || g_video_list.count == 0)
    {
        LOG_WRN("no AVI files found (%d)", ret);
        return (ret != 0) ? ret : -ENOENT;
    }

    /* 显示列表 */
    lvgl_sd_video_show_list();
    return 0;
}
