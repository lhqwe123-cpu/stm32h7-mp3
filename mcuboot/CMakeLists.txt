# ============================================================
# MCUboot Bootloader for STM32H743IIT6
#
# 编译: west build -b stm32h743iit6 mcuboot
# ============================================================

cmake_minimum_required(VERSION 3.20.0)

# ---- 板级 overlay ----
set(EXTRA_DTC_OVERLAY_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/stm32h743iit6.overlay"
    CACHE FILEPATH "Board overlay"
)

# ---- 签名密钥 ----
set(BOOT_SIGNATURE_KEY_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/keys/root-ec-p256.pem"
    CACHE FILEPATH "ECDSA P256 signing key"
)

# ---- Zephyr 初始化 ----
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(MCUboot)

# ---- MCUboot 模块路径 ----
set(MCUBOOT_DIR ${ZEPHYR_MCUBOOT_MODULE_DIR})
set(BOOT_DIR   ${MCUBOOT_DIR}/boot)
set(TINYCRYPT_DIR "${MCUBOOT_DIR}/ext/tinycrypt/lib")
assert_exists(TINYCRYPT_DIR)

# ---- mbedTLS ASN1 ----
if(NOT CONFIG_MBEDTLS_BUILTIN AND NOT CONFIG_BOOT_KEY_IMPORT_BYPASS_ASN)
    set(MBEDTLS_ASN1_DIR "${MCUBOOT_DIR}/ext/mbedtls-asn1")
    assert_exists(MBEDTLS_ASN1_DIR)
    zephyr_include_directories(${MBEDTLS_ASN1_DIR}/include)
    zephyr_sources(
        ${MBEDTLS_ASN1_DIR}/src/asn1parse.c
        ${MBEDTLS_ASN1_DIR}/src/platform_util.c
    )
endif()

# ---- 头文件 ----
zephyr_include_directories(${BOOT_DIR}/zephyr/include)
zephyr_include_directories(${BOOT_DIR}/bootutil/include)

# ---- MCUboot 核心 ----
target_sources(app PRIVATE
    ${BOOT_DIR}/zephyr/main.c
    ${BOOT_DIR}/zephyr/io.c
    ${BOOT_DIR}/zephyr/flash_map_extended.c
    ${BOOT_DIR}/zephyr/os.c
    ${BOOT_DIR}/zephyr/keys.c
    ${BOOT_DIR}/zephyr/watchdog.c
)

# ---- bootutil ----
zephyr_sources(
    ${BOOT_DIR}/bootutil/src/image_validate.c
    ${BOOT_DIR}/bootutil/src/bootutil_find_key.c
    ${BOOT_DIR}/bootutil/src/bootutil_img_hash.c
    ${BOOT_DIR}/bootutil/src/bootutil_img_security_cnt.c
    ${BOOT_DIR}/bootutil/src/tlv.c
    ${BOOT_DIR}/bootutil/src/encrypted.c
    ${BOOT_DIR}/bootutil/src/image_rsa.c
    ${BOOT_DIR}/bootutil/src/image_ecdsa.c
    ${BOOT_DIR}/bootutil/src/image_ed25519.c
    ${BOOT_DIR}/bootutil/src/bootutil_misc.c
    ${BOOT_DIR}/bootutil/src/bootutil_area.c
    ${BOOT_DIR}/bootutil/src/bootutil_loader.c
    ${BOOT_DIR}/bootutil/src/fault_injection_hardening.c
    ${BOOT_DIR}/bootutil/src/loader.c
    ${BOOT_DIR}/bootutil/src/swap_misc.c
    ${BOOT_DIR}/bootutil/src/caps.c
)

# ---- swap 策略 ----
if(CONFIG_BOOT_SWAP_USING_MOVE)
    zephyr_sources(${BOOT_DIR}/bootutil/src/swap_move.c)
elseif(CONFIG_BOOT_SWAP_USING_SCRATCH)
    zephyr_sources(${BOOT_DIR}/bootutil/src/swap_scratch.c)
endif()

# ---- ECDSA P256 + tinycrypt ----
if(CONFIG_BOOT_ECDSA_TINYCRYPT)
    zephyr_include_directories(${TINYCRYPT_DIR}/include)
    zephyr_sources(
        ${TINYCRYPT_DIR}/source/ecc.c
        ${TINYCRYPT_DIR}/source/ecc_dh.c
        ${TINYCRYPT_DIR}/source/ecc_dsa.c
        ${TINYCRYPT_DIR}/source/ecc_platform_specific.c
        ${TINYCRYPT_DIR}/source/sha256.c
        ${TINYCRYPT_DIR}/source/utils.c
    )
endif()

# ---- 密钥提取 ----
if(NOT CONFIG_BOOT_SIGNATURE_TYPE_NONE)
    set(KEY_FILE "${BOOT_SIGNATURE_KEY_FILE}")
    if(EXISTS "${KEY_FILE}")
        set(GENERATED_PUBKEY ${ZEPHYR_BINARY_DIR}/autogen-pubkey.c)
        add_custom_command(
            OUTPUT ${GENERATED_PUBKEY}
            COMMAND ${PYTHON_EXECUTABLE}
                ${MCUBOOT_DIR}/scripts/imgtool.py getpub
                -k "${KEY_FILE}"
                > ${GENERATED_PUBKEY}
            DEPENDS "${KEY_FILE}"
            COMMENT "Extracting public key"
        )
        target_sources(app PRIVATE ${GENERATED_PUBKEY})
    else()
        message(FATAL_ERROR
            "Key not found: ${KEY_FILE}\n"
            "Generate: openssl ecparam -name prime256v1 -genkey -noout -out ${KEY_FILE}"
        )
    endif()
endif()
