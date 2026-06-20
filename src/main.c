#include "main.h"
#include "led_thread.h"
#include "video_thread.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* SD 卡文件系统 */
static FATFS g_sd_fatfs;
static struct fs_mount_t sd_mount = {
    .type = FS_FATFS,
    .mnt_point = "/SD:",
    .fs_data = &g_sd_fatfs,
};

/* SD 卡自动挂载 */
static int sdcard_auto_mount(void)
{
    static const char *disk = "SD";
    int ret;
    for (int i = 0; i < 5; i++)
    {
        ret = disk_access_init(disk);
        if (ret == 0)
            break;
        k_sleep(K_MSEC(100));
    }
    if (ret != 0)
    {
        LOG_ERR("disk_access_init failed (%d)", ret);
        return ret;
    }
    ret = fs_mount(&sd_mount);
    if (ret != 0)
    {
        LOG_ERR("fs_mount failed (%d)", ret);
        return ret;
    }
    LOG_INF("mounted at /SD:");
    return 0;
}

int main(void)
{
    /* ========== 主线程：基础硬件初始化 ========== */
    my_heap_init();
    sdcard_auto_mount();

    /* ========== 创建所有子线程（K_FOREVER 挂起，暂不调度） ========== */

    /* LED闪烁线程 */
    if (led_thread_init() != 0)
    {
        LOG_ERR("led_thread_init failed!");
        return -1;
    }

    /* LVGL 显示线程 */
    if (video_thread_init() != 0)
    {
        LOG_ERR("video_thread_init failed!");
        return -1;
    }

    /* ========== 统一启动所有子线程，确保同步开始调度 ========== */
    LOG_INF("starting all threads...");
    led_thread_start();
    video_thread_start();

    /* 主线程退出，子线程继续运行 */
    LOG_INF("init done, exiting");
    return 0;
}
