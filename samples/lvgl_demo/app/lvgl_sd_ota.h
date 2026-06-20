#ifndef __LVGL_SD_OTA_H__
#define __LVGL_SD_OTA_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief OTA 固件包列表条目
     */
    typedef struct
    {
        char name[256];
        char path[280];
    } ota_fwpkg_entry_t;

/**
 * @brief OTA 固件包列表
 */
#define OTA_FWPKG_LIST_MAX 32
    typedef struct
    {
        ota_fwpkg_entry_t entries[OTA_FWPKG_LIST_MAX];
        int count;
    } ota_fwpkg_list_t;

    /**
     * @brief 扫描固件包目录，获取列表
     * @param list 输出列表
     * @return 0 成功，负值失败
     */
    int ota_scan_fwpkg_list(ota_fwpkg_list_t *list);

    /**
     * @brief 启动 OTA 升级界面（固件包列表）
     * @return 0 成功
     */
    int lvgl_sd_ota_start(void);

    /**
     * @brief LVGL 事件回调包装 (用于 lv_obj_add_event_cb)
     */
    void lvgl_sd_ota_start_cb(lv_event_t *e);

    /**
     * @brief 创建 OTA 升级界面 screen
     * @return screen 对象
     */
    void *lvgl_sd_ota_create_screen(void);

    /**
     * @brief 设置返回主菜单的回调函数
     */
    void lvgl_sd_ota_set_back_to_menu_cb(lv_event_cb_t cb);

    /**
     * @brief 刷新 OTA 升级进度 UI（由 LVGL 主循环调用）
     */
    void lvgl_sd_ota_progress_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __LVGL_SD_OTA_H__ */
