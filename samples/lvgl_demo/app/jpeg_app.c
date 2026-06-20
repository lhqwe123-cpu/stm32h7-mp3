#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/drivers/display.h>
#include <zephyr/cache.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

LOG_MODULE_REGISTER(jpeg_app, LOG_LEVEL_INF);

#include "lcd.h"
#include "my_malloc.h"
#include "jpegcodec.h"

#define JPEG_SCAN_DIR_USER "0:/PICTURE"
#define JPEG_SCAN_DIR_FS "/SD:/PICTURE"
#define JPEG_MAX_FILE_SIZE (4U * 1024U * 1024U)

static bool has_jpeg_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    char ext[6];

    if (dot == NULL)
    {
        return false;
    }

    dot++;
    if (*dot == '\0')
    {
        return false;
    }

    size_t len = strlen(dot);
    if (len >= sizeof(ext))
    {
        return false;
    }

    for (size_t i = 0; i < len; i++)
    {
        ext[i] = (char)tolower((unsigned char)dot[i]);
    }
    ext[len] = '\0';

    return (strcmp(ext, "jpg") == 0) || (strcmp(ext, "jpeg") == 0) || (strcmp(ext, "jpe") == 0);
}

static int find_first_jpeg(char *out_path, size_t out_path_len)
{
    struct fs_dir_t dir;
    struct fs_dirent ent;
    int ret;

    fs_dir_t_init(&dir);
    ret = fs_opendir(&dir, JPEG_SCAN_DIR_FS);
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

        if (!has_jpeg_ext(ent.name))
        {
            continue;
        }

        (void)snprintf(out_path, out_path_len, "%s/%.*s", JPEG_SCAN_DIR_FS, (int)(out_path_len - strlen(JPEG_SCAN_DIR_FS) - 2), ent.name);
        fs_closedir(&dir);
        return 0;
    }
}

static int read_file_to_buffer(const char *path, uint8_t **out_buf, uint32_t *out_size)
{
    struct fs_file_t file;
    struct fs_dirent st;
    int ret;
    ssize_t rd;
    uint8_t *buf;

    *out_buf = NULL;
    *out_size = 0;

    memset(&st, 0, sizeof(st));
    ret = fs_stat(path, &st);
    if (ret != 0)
    {
        return ret;
    }
    if ((st.type != FS_DIR_ENTRY_FILE) || (st.size == 0) || (st.size > JPEG_MAX_FILE_SIZE))
    {
        return -EINVAL;
    }

    buf = mymalloc(SRAMEX, st.size);
    if (buf == NULL)
    {
        return -ENOMEM;
    }

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
    (void)sys_cache_data_flush_range(buf, st.size);

    *out_buf = buf;
    *out_size = st.size;
    return 0;
}

int jpeg_show_first_picture(void)
{
    char path[192];
    uint8_t *jpeg_buf = NULL;
    uint32_t jpeg_size = 0U;
    uint16_t *rgb_buf = NULL;
    uint16_t img_w = 0U;
    uint16_t img_h = 0U;
    struct display_buffer_descriptor desc;
    int ret;

    ret = find_first_jpeg(path, sizeof(path));

    if (ret != 0)
    {
        LOG_WRN("no jpg/jpeg/jpe in %s (%d)", JPEG_SCAN_DIR_USER, ret);
        return ret;
    }

    ret = read_file_to_buffer(path, &jpeg_buf, &jpeg_size);
    if (ret != 0)
    {
        LOG_ERR("read failed %s (%d)", path, ret);
        return ret;
    }

    /* 多分配 64KB 防止 DMA2D 越界损坏 heap */
    rgb_buf = mymalloc(SRAMEX, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t) + 65536);
    if (rgb_buf == NULL)
    {
        myfree(SRAMEX, jpeg_buf);
        return -ENOMEM;
    }
    memset(rgb_buf, 0, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t));
    (void)sys_cache_data_flush_range(rgb_buf, (uint32_t)LCD_W * LCD_H * sizeof(uint16_t));

    ret = jpeg_decode_to_rgb565(jpeg_buf, jpeg_size, rgb_buf, LCD_W, LCD_H, &img_w, &img_h);
    myfree(SRAMEX, jpeg_buf);
    if (ret != 0)
    {
        myfree(SRAMEX, rgb_buf);
        LOG_ERR("decode failed (%d)", ret);
        return ret;
    }

    desc.width = img_w;
    desc.height = img_h;
    desc.pitch = img_w;
    desc.buf_size = (size_t)img_w * img_h * sizeof(uint16_t);

    /* 用 DMA2D 把解码数据搬到 framebuffer，避免 display_write 把 front_buf 切到堆地址 */
    __HAL_RCC_DMA2D_CLK_ENABLE();
    DMA2D->CR &= ~(DMA2D_CR_START);
    DMA2D->CR = DMA2D_M2M;
    DMA2D->FGPFCCR = DMA2D_OUTPUT_RGB565;
    DMA2D->FGOR = 0;
    DMA2D->OOR = LCD_W - img_w;
    DMA2D->FGMAR = (uint32_t)rgb_buf;
    DMA2D->OMAR = (uint32_t)fr_addr;
    DMA2D->NLR = (img_w << DMA2D_NLR_PL_Pos) | (img_h << DMA2D_NLR_NL_Pos);
    DMA2D->CR |= DMA2D_CR_START;
    while ((DMA2D->ISR & DMA2D_FLAG_TC) == 0)
        ;
    DMA2D->IFCR |= DMA2D_FLAG_TC;

    myfree(SRAMEX, rgb_buf);

    /* 提交 framebuffer 地址给驱动显示 */
    ret = display_write(disp, 0, 0, &desc, fr_addr);
    if (ret != 0)
    {
        LOG_ERR("display_write failed (%d)", ret);
        return ret;
    }

    LOG_INF("shown %s (%ux%u)", path, img_w, img_h);
    return 0;
}
