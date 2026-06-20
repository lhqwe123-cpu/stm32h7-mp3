#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

LOG_MODULE_REGISTER(lvgl_sd_pic, LOG_LEVEL_INF);

#include "lvgl.h"
#include "lcd.h"
#include "my_malloc.h"
#include "jpegcodec.h"
#include "lvgl_sd_pic.h"

/* SD 卡上 JPEG 图片扫描目录 */
#define SD_PIC_SCAN_DIR "/SD:/PICTURE"
#define SD_PIC_MAX_SIZE (4U * 1024U * 1024U)

/* LVGL 图片缓存 */
static lv_image_dsc_t g_sd_img_dsc;
static uint8_t *g_sd_img_data;

/* 判断文件是否为 JPEG 扩展名 */
static bool has_jpeg_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL)
        return false;
    dot++;
    if (*dot == '\0')
        return false;

    char ext[6];
    size_t len = strlen(dot);
    if (len >= sizeof(ext))
        return false;
    for (size_t i = 0; i < len; i++)
    {
        ext[i] = (char)tolower((unsigned char)dot[i]);
    }
    ext[len] = '\0';

    return (strcmp(ext, "jpg") == 0) || (strcmp(ext, "jpeg") == 0) || (strcmp(ext, "jpe") == 0);
}

/* 扫描目录找到第一个 JPEG 文件 */
static int find_first_jpeg(char *out_path, size_t out_path_len)
{
    struct fs_dir_t dir;
    struct fs_dirent ent;
    int ret;

    fs_dir_t_init(&dir);
    ret = fs_opendir(&dir, SD_PIC_SCAN_DIR);
    if (ret != 0)
        return ret;

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
            continue;
        if (!has_jpeg_ext(ent.name))
            continue;

        (void)snprintf(out_path, out_path_len, "%s/%.*s", SD_PIC_SCAN_DIR, (int)(out_path_len - strlen(SD_PIC_SCAN_DIR) - 2), ent.name);
        fs_closedir(&dir);
        return 0;
    }
}

/* 读取整个文件到缓冲区 */
static int read_file_to_buffer(const char *path, uint8_t **out_buf, uint32_t *out_size)
{
    struct fs_file_t file;
    struct fs_dirent st;
    int ret;
    ssize_t rd;

    *out_buf = NULL;
    *out_size = 0;

    memset(&st, 0, sizeof(st));
    ret = fs_stat(path, &st);
    if (ret != 0)
        return ret;
    if ((st.type != FS_DIR_ENTRY_FILE) || (st.size == 0) || (st.size > SD_PIC_MAX_SIZE))
        return -EINVAL;

    uint8_t *buf = mymalloc(SRAMEX, st.size);
    if (buf == NULL)
        return -ENOMEM;

    fs_file_t_init(&file);
    ret = fs_open(&file, path, FS_O_READ);
    if (ret != 0)
    {
        myfree(SRAMEX, buf);
        return ret;
    }

    rd = fs_read(&file, buf, st.size);
    (void)fs_close(&file);
    if (rd < 0 || (size_t)rd != st.size)
    {
        myfree(SRAMEX, buf);
        return -EIO;
    }

    *out_buf = buf;
    *out_size = st.size;
    return 0;
}

/**
 * @brief  从 SD 卡读取 JPEG 图片，解码后用 LVGL 显示
 *
 * 流程：
 *   1. 扫描 SD 卡 PICTURE 目录找到第一个 JPEG 文件
 *   2. 读取 JPEG 文件到内存
 *   3. 用硬件 JPEG 解码器解码为 RGB565
 *   4. 将 RGB565 数据注册为 LVGL 图片对象
 *   5. 创建 LVGL 界面：图片 + 标注文字
 */
void lvgl_sd_picture_show(void)
{
    char path[192];
    uint8_t *jpeg_buf = NULL;
    uint32_t jpeg_size = 0;
    uint16_t *rgb_buf = NULL;
    uint16_t img_w = 0, img_h = 0;
    int ret;

    /* ---- 1. 扫描 SD 卡找到 JPEG 文件 ---- */
    ret = find_first_jpeg(path, sizeof(path));
    if (ret != 0)
    {
        LOG_WRN("no JPEG found in %s (%d)", SD_PIC_SCAN_DIR, ret);
        return;
    }
    LOG_INF("found %s", path);

    /* ---- 2. 读取 JPEG 文件 ---- */
    ret = read_file_to_buffer(path, &jpeg_buf, &jpeg_size);
    if (ret != 0)
    {
        LOG_ERR("read failed %s (%d)", path, ret);
        return;
    }
    LOG_INF("read %u bytes", (unsigned int)jpeg_size);

    /* ---- 3. 分配 RGB565 输出缓冲区 ---- */
    rgb_buf = mymalloc(SRAMEX, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t) + 65536);
    if (rgb_buf == NULL)
    {
        LOG_ERR("rgb buf alloc failed");
        myfree(SRAMEX, jpeg_buf);
        return;
    }
    memset(rgb_buf, 0, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t));

    /* ---- 4. 硬件 JPEG 解码 ---- */
    ret = jpeg_decode_to_rgb565(jpeg_buf, jpeg_size, rgb_buf, LCD_W, LCD_H, &img_w, &img_h);
    myfree(SRAMEX, jpeg_buf);
    if (ret != 0)
    {
        LOG_ERR("decode failed (%d)", ret);
        myfree(SRAMEX, rgb_buf);
        return;
    }
    LOG_INF("decoded %ux%u", (unsigned int)img_w, (unsigned int)img_h);

    /* ---- 5. 注册为 LVGL 图片描述符 ---- */
    /* 释放旧图片数据 */
    if (g_sd_img_data != NULL)
    {
        myfree(SRAMEX, g_sd_img_data);
    }
    g_sd_img_data = (uint8_t *)rgb_buf;

    g_sd_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_sd_img_dsc.header.w = img_w;
    g_sd_img_dsc.header.h = img_h;
    g_sd_img_dsc.data_size = (uint32_t)img_w * img_h * sizeof(uint16_t);
    g_sd_img_dsc.data = (const uint8_t *)rgb_buf;

    /* ---- 6. 创建 LVGL 界面 ---- */
    /* 创建屏幕 */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    /* 创建图片对象，居中显示 */
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &g_sd_img_dsc);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    /* 创建标注标签 — 叠加在图片底部 */
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "这是 LVGL 显示的图片 (SD卡)");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* 创建副标题 */
    lv_obj_t *sub_label = lv_label_create(scr);
    lv_label_set_text_fmt(sub_label, "文件: %s  |  分辨率: %ux%u", path, img_w, img_h);
    lv_obj_set_style_text_color(sub_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(sub_label, &lv_font_montserrat_14, 0);
    lv_obj_align(sub_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    LOG_INF("display done");
}

/* 获取SD卡解码后的图片数据（用于其他界面作为背景） */
const lv_image_dsc_t *lvgl_sd_pic_get_image(void)
{
    if (g_sd_img_data == NULL)
    {
        /* 尝试加载图片 */
        char path[192];
        uint8_t *jpeg_buf = NULL;
        uint32_t jpeg_size = 0;
        uint16_t *rgb_buf = NULL;
        uint16_t img_w = 0, img_h = 0;
        int ret;

        ret = find_first_jpeg(path, sizeof(path));
        if (ret != 0)
        {
            LOG_WRN("get_image no JPEG found (%d)", ret);
            return NULL;
        }

        ret = read_file_to_buffer(path, &jpeg_buf, &jpeg_size);
        if (ret != 0)
        {
            LOG_ERR("get_image read failed (%d)", ret);
            return NULL;
        }

        rgb_buf = mymalloc(SRAMEX, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t) + 65536);
        if (rgb_buf == NULL)
        {
            LOG_ERR("get_image rgb buf alloc failed");
            myfree(SRAMEX, jpeg_buf);
            return NULL;
        }
        memset(rgb_buf, 0, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t));

        ret = jpeg_decode_to_rgb565(jpeg_buf, jpeg_size, rgb_buf, LCD_W, LCD_H, &img_w, &img_h);
        myfree(SRAMEX, jpeg_buf);
        if (ret != 0)
        {
            LOG_ERR("get_image decode failed (%d)", ret);
            myfree(SRAMEX, rgb_buf);
            return NULL;
        }

        g_sd_img_data = (uint8_t *)rgb_buf;
        g_sd_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        g_sd_img_dsc.header.w = img_w;
        g_sd_img_dsc.header.h = img_h;
        g_sd_img_dsc.data_size = (uint32_t)img_w * img_h * sizeof(uint16_t);
        g_sd_img_dsc.data = (const uint8_t *)rgb_buf;
    }

    return &g_sd_img_dsc;
}

/* 释放 lvgl_sd_pic 模块持有的图片资源 */
void lvgl_sd_pic_release(void)
{
    if (g_sd_img_data != NULL)
    {
        myfree(SRAMEX, g_sd_img_data);
        g_sd_img_data = NULL;
    }
    memset(&g_sd_img_dsc, 0, sizeof(g_sd_img_dsc));
}
