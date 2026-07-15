cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(hslock_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(MBEDTLS_CONFIG_FILE "mbedtls_config.h")

pico_sdk_init()


# ---------------------------------------------------------------------------
# LittleFS - compiled directly from submodule source
# ---------------------------------------------------------------------------
 
add_library(littlefs STATIC
    libs/littlefs/lfs.c
    libs/littlefs/lfs_util.c
)
target_include_directories(littlefs PUBLIC libs/littlefs)
target_compile_definitions(littlefs PUBLIC
    LFS_NO_MALLOC       # use static allocation only
    LFS_NO_DEBUG        # strip debug prints in release
)

# ---------------------------------------------------------------------------
# base64
# ---------------------------------------------------------------------------

add_library(base64 STATIC
    libs/base64/base64.c
)
target_include_directories(base64 PUBLIC libs/base64)

# ---------------------------------------------------------------------------
# base32 + qrcodegen
# ---------------------------------------------------------------------------

add_library(base32 STATIC libs/base32/base32.c)
target_include_directories(base32 PUBLIC libs/base32)

add_library(qrcodegen STATIC libs/qrcodegen/c/qrcodegen.c)
target_include_directories(qrcodegen PUBLIC libs/qrcodegen/c)

# ---------------------------------------------------------------------------
# Project sources
# ---------------------------------------------------------------------------

# Get git commit hash
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()

# Detect uncommitted changes
execute_process(
    COMMAND git diff --quiet HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE GIT_DIRTY
    ERROR_QUIET
)
# Returns 0 if clean, 1 if dirty
if(NOT GIT_DIRTY EQUAL 0)
    set(GIT_DIRTY 1)
else()
    set(GIT_DIRTY 0)
endif()

string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M UTC" UTC)

configure_file(
    ${CMAKE_SOURCE_DIR}/version.h.in
    ${CMAKE_BINARY_DIR}/version.h
)

file(GLOB SERIAL_SOURCES   CONFIGURE_DEPENDS "serial/*.c")
file(GLOB HARDWARE_SOURCES CONFIGURE_DEPENDS "hardware/*.c")
file(GLOB NETWORK_SOURCES CONFIGURE_DEPENDS "network/*.c")
file(GLOB STORAGE_SOURCES  CONFIGURE_DEPENDS "storage/*.c")
file(GLOB SHARED_SOURCES   CONFIGURE_DEPENDS "shared/*.c")

add_executable(hslock
    main.c
    ${SERIAL_SOURCES}
    ${HARDWARE_SOURCES}
    ${NETWORK_SOURCES}
    ${STORAGE_SOURCES}
    ${SHARED_SOURCES}
)

target_include_directories(hslock PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(hslock
    pico_stdlib
    pico_mbedtls
    pico_multicore
    pico_flash       # flash_safe_execute
    pico_rand                                              
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_rtc
    hardware_pwm
    hardware_flash
    littlefs
    base64
    qrcodegen
    base32
)

pico_enable_stdio_usb(hslock 1)
pico_enable_stdio_uart(hslock 0)

pico_add_extra_outputs(hslock)