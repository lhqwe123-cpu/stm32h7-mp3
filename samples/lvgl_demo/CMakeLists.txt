cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lvgl_demo)

# ============================================================
# 自行移植的 LVGL 路径 (不使用 Zephyr 内核自带的 lvgl 模块)
# ============================================================
set(LVGL_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/LVGL)
set(LVGL_SRC_DIR  ${LVGL_ROOT_DIR}/GUI/lvgl/src)
set(LVGL_DEMO_DIR ${LVGL_ROOT_DIR}/GUI_APP/demos)

# --- lib 驱动源文件 ---
file(GLOB LIB_DRIVER_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c
)

# --- LVGL 核心源文件 (使用 .mk 中的文件列表思路，显式 glob 各子目录) ---
file(GLOB_RECURSE LVGL_CORE_SOURCES CONFIGURE_DEPENDS
  ${LVGL_SRC_DIR}/core/*.c
  ${LVGL_SRC_DIR}/draw/*.c
  ${LVGL_SRC_DIR}/draw/sw/*.c
  ${LVGL_SRC_DIR}/extra/*.c
  ${LVGL_SRC_DIR}/extra/layouts/flex/*.c
  ${LVGL_SRC_DIR}/extra/layouts/grid/*.c
  ${LVGL_SRC_DIR}/extra/themes/default/*.c
  ${LVGL_SRC_DIR}/extra/themes/basic/*.c
  ${LVGL_SRC_DIR}/extra/themes/mono/*.c
  ${LVGL_SRC_DIR}/extra/widgets/animimg/*.c
  ${LVGL_SRC_DIR}/extra/widgets/calendar/*.c
  ${LVGL_SRC_DIR}/extra/widgets/chart/*.c
  ${LVGL_SRC_DIR}/extra/widgets/colorwheel/*.c
  ${LVGL_SRC_DIR}/extra/widgets/imgbtn/*.c
  ${LVGL_SRC_DIR}/extra/widgets/keyboard/*.c
  ${LVGL_SRC_DIR}/extra/widgets/led/*.c
  ${LVGL_SRC_DIR}/extra/widgets/list/*.c
  ${LVGL_SRC_DIR}/extra/widgets/menu/*.c
  ${LVGL_SRC_DIR}/extra/widgets/meter/*.c
  ${LVGL_SRC_DIR}/extra/widgets/msgbox/*.c
  ${LVGL_SRC_DIR}/extra/widgets/span/*.c
  ${LVGL_SRC_DIR}/extra/widgets/spinbox/*.c
  ${LVGL_SRC_DIR}/extra/widgets/spinner/*.c
  ${LVGL_SRC_DIR}/extra/widgets/tabview/*.c
  ${LVGL_SRC_DIR}/extra/widgets/tileview/*.c
  ${LVGL_SRC_DIR}/extra/widgets/win/*.c
  ${LVGL_SRC_DIR}/extra/others/snapshot/*.c
  ${LVGL_SRC_DIR}/extra/others/monkey/*.c
  ${LVGL_SRC_DIR}/extra/others/gridnav/*.c
  ${LVGL_SRC_DIR}/font/*.c
  ${LVGL_SRC_DIR}/hal/*.c
  ${LVGL_SRC_DIR}/misc/*.c
  ${LVGL_SRC_DIR}/widgets/*.c
)

# --- LVGL demo 源文件 ---
file(GLOB_RECURSE LVGL_DEMO_SOURCES CONFIGURE_DEPENDS
  ${LVGL_DEMO_DIR}/*.c
)

# --- LVGL porting 模板源文件 ---
file(GLOB LVGL_PORTING_SOURCES CONFIGURE_DEPENDS
  ${LVGL_ROOT_DIR}/GUI/lvgl/examples/porting/*.c
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

# --- 应用源文件 ---
target_sources(app PRIVATE
  src/main.c
  src/video_thread.c
  src/led_thread.c
  ${APP_SOURCES}
  ${LIB_DRIVER_SOURCES}
  ${BOOTLOADER_APP_SOURCES}
  ${BOOTLOADER_LIB_SOURCES}
  ${LVGL_CORE_SOURCES}
  ${LVGL_DEMO_SOURCES}
  ${LVGL_PORTING_SOURCES}
)

# --- 头文件路径 ---
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/app
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/app
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/lib
  # LVGL 核心头文件路径 (lvgl.h 内部使用 src/xxx/xxx.h 相对路径)
  ${LVGL_ROOT_DIR}/GUI/lvgl
  # LVGL src 子目录 (部分内部 include 直接写文件名)
  ${LVGL_SRC_DIR}
  ${LVGL_SRC_DIR}/core
  ${LVGL_SRC_DIR}/draw
  ${LVGL_SRC_DIR}/draw/sw
  ${LVGL_SRC_DIR}/extra
  ${LVGL_SRC_DIR}/extra/layouts
  ${LVGL_SRC_DIR}/extra/layouts/flex
  ${LVGL_SRC_DIR}/extra/layouts/grid
  ${LVGL_SRC_DIR}/extra/themes
  ${LVGL_SRC_DIR}/extra/themes/default
  ${LVGL_SRC_DIR}/extra/themes/basic
  ${LVGL_SRC_DIR}/extra/themes/mono
  ${LVGL_SRC_DIR}/extra/widgets
  ${LVGL_SRC_DIR}/extra/widgets/animimg
  ${LVGL_SRC_DIR}/extra/widgets/calendar
  ${LVGL_SRC_DIR}/extra/widgets/chart
  ${LVGL_SRC_DIR}/extra/widgets/colorwheel
  ${LVGL_SRC_DIR}/extra/widgets/imgbtn
  ${LVGL_SRC_DIR}/extra/widgets/keyboard
  ${LVGL_SRC_DIR}/extra/widgets/led
  ${LVGL_SRC_DIR}/extra/widgets/list
  ${LVGL_SRC_DIR}/extra/widgets/menu
  ${LVGL_SRC_DIR}/extra/widgets/meter
  ${LVGL_SRC_DIR}/extra/widgets/msgbox
  ${LVGL_SRC_DIR}/extra/widgets/span
  ${LVGL_SRC_DIR}/extra/widgets/spinbox
  ${LVGL_SRC_DIR}/extra/widgets/spinner
  ${LVGL_SRC_DIR}/extra/widgets/tabview
  ${LVGL_SRC_DIR}/extra/widgets/tileview
  ${LVGL_SRC_DIR}/extra/widgets/win
  ${LVGL_SRC_DIR}/extra/others
  ${LVGL_SRC_DIR}/extra/others/snapshot
  ${LVGL_SRC_DIR}/extra/others/monkey
  ${LVGL_SRC_DIR}/extra/others/gridnav
  ${LVGL_SRC_DIR}/extra/libs
  ${LVGL_SRC_DIR}/extra/libs/bmp
  ${LVGL_SRC_DIR}/extra/libs/fsdrv
  ${LVGL_SRC_DIR}/extra/libs/png
  ${LVGL_SRC_DIR}/extra/libs/gif
  ${LVGL_SRC_DIR}/extra/libs/qrcode
  ${LVGL_SRC_DIR}/extra/libs/sjpg
  ${LVGL_SRC_DIR}/extra/libs/freetype
  ${LVGL_SRC_DIR}/extra/libs/rlottie
  ${LVGL_SRC_DIR}/extra/libs/ffmpeg
  ${LVGL_SRC_DIR}/font
  ${LVGL_SRC_DIR}/hal
  ${LVGL_SRC_DIR}/misc
  ${LVGL_SRC_DIR}/widgets
  # LVGL demo 头文件路径
  ${LVGL_DEMO_DIR}
  ${LVGL_DEMO_DIR}/benchmark
  ${LVGL_DEMO_DIR}/keypad_encoder
  ${LVGL_DEMO_DIR}/music
  ${LVGL_DEMO_DIR}/stress
  ${LVGL_DEMO_DIR}/widgets
  # LVGL porting 模板头文件路径
  ${LVGL_ROOT_DIR}/GUI/lvgl/examples/porting
)


