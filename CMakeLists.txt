cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mp3)

# --- lib 驱动源文件 ---
file(GLOB LIB_DRIVER_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c
)

# --- app 源文件 ---
file(GLOB APP_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c
)

# --- bootloader app 源文件 (MCUboot 升级相关) ---
file(GLOB BOOTLOADER_APP_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/app/*.c
)

# --- bootloader lib 源文件 (MCUboot 辅助库) ---
file(GLOB BOOTLOADER_LIB_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/lib/*.c
)

# --- hdiffpatch 差分库源文件 ---
file(GLOB HDIFFPATCH_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/lib/hdiffpatch/*.c
)

# --- 应用源文件 ---
target_sources(app PRIVATE
  src/main.c
  src/led_thread.c
  src/video_thread.c
  ${APP_SOURCES}
  ${LIB_DRIVER_SOURCES}
  ${BOOTLOADER_APP_SOURCES}
  ${BOOTLOADER_LIB_SOURCES}
  ${HDIFFPATCH_SOURCES}
)

# --- 头文件路径 ---
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/app
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/app
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/lib/hdiffpatch
)

# --- HDiffPatch 编译选项 (单线程嵌入式模式) ---
target_compile_definitions(app PRIVATE
  _IS_USED_MULTITHREAD=0
  _IS_NEED_CACHE_OLD_BY_COVERS=0
  _IS_NEED_CACHE_OLD_ALL=0
)

# ============================================================
# Code/Data Relocation: LVGL hot path -> ITCM + DTCM
# ============================================================
# ITCM (64KB):  LVGL render pipeline hot functions (.text only)
# DTCM (128KB): LVGL BSS (zero-init data, via __section__ attribute)
# NOTE: DMA buffers must NOT go to DTCM (DTCM is not DMA-accessible)
if(CONFIG_CODE_DATA_RELOCATION)
  # Collect LVGL core source files
  file(GLOB_RECURSE LVGL_CORE_SOURCES
    ${ZEPHYR_LVGL_MODULE_DIR}/src/core/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/draw/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/display/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/indev/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/widgets/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/themes/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/layouts/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/font/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/misc/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/others/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/stdlib/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/tick/*.c
    ${ZEPHYR_LVGL_MODULE_DIR}/src/lv_init.c
  )

  # ---- ITCM: LVGL render hot path (.text only) ----
  zephyr_code_relocate(
    FILES ${LVGL_CORE_SOURCES}
    FILTER ".*lv_blend_to_rgb565.*|.*lv_blend_fill.*|.*lv_draw_sw_blend\\b.*|.*lv_draw_sw_fill.*|.*lv_draw_sw_img.*|.*lv_draw_sw_border.*|.*lv_draw_sw_letter.*|.*lv_draw_sw_transform.*|.*lv_draw_sw_mask.*|.*lv_draw_sw_grad.*|.*lv_draw_sw_box_shadow.*|.*lv_draw_sw_arc.*|.*lv_draw_sw_line.*|.*lv_draw_sw_triangle.*|.*lv_draw_sw_vector.*|.*lv_draw_sw_blur.*|.*lv_draw_sw_utils.*|.*lv_draw_sw\\b.*|.*lv_font_fmt_txt.*|.*lv_image_decoder.*|.*lv_draw_label.*|.*lv_draw_image.*|.*lv_draw_rect.*|.*lv_draw_arc.*|.*lv_draw_line.*|.*lv_draw_mask.*|.*lv_draw_blur.*|.*lv_draw_triangle.*|.*lv_draw_vector.*|.*lv_area\\b.*|.*lv_refr\\b.*|.*lv_draw_buf\\b.*|.*lv_display_flush.*"
    LOCATION ITCM
  )
endif()

