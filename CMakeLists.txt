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

# --- 应用源文件 ---
target_sources(app PRIVATE
  src/main.c
  src/led_thread.c
  src/video_thread.c
  ${APP_SOURCES}
  ${LIB_DRIVER_SOURCES}
  ${BOOTLOADER_APP_SOURCES}
  ${BOOTLOADER_LIB_SOURCES}
)

# --- 头文件路径 ---
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/app
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/app
  ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/lib
)

