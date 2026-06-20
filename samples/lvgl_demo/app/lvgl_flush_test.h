#ifndef __LVGL_FLUSH_TEST_H__
#define __LVGL_FLUSH_TEST_H__

#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 启动全屏刷新测试页面
     * @return 0 成功
     */
    int lvgl_flush_test_start(void);

    /**
     * @brief 设置返回主菜单的回调函数
     */
    void lvgl_flush_test_set_back_cb(lv_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __LVGL_FLUSH_TEST_H__ */
