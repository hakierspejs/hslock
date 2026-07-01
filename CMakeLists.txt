cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(hslock_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()


# ---------------------------------------------------------------------------
# LittleFS — compiled directly from submodule source
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
# Project sources
# ---------------------------------------------------------------------------

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

string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M UTC" UTC)

configure_file(
    ${CMAKE_SOURCE_DIR}/version.h.in
    ${CMAKE_BINARY_DIR}/version.h
)

file(GLOB SERIAL_SOURCES   CONFIGURE_DEPENDS "serial/*.c")
file(GLOB HARDWARE_SOURCES CONFIGURE_DEPENDS "hardware/*.c")
file(GLOB INTERNET_SOURCES CONFIGURE_DEPENDS "internet/*.c")
file(GLOB STORAGE_SOURCES  CONFIGURE_DEPENDS "storage/*.c")
file(GLOB SHARED_SOURCES   CONFIGURE_DEPENDS "shared/*.c")

add_executable(hslock
    main.c
    ${SERIAL_SOURCES}
    ${HARDWARE_SOURCES}
    ${INTERNET_SOURCES}
    ${STORAGE_SOURCES}
    ${SHARED_SOURCES}
)

target_include_directories(hslock PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(hslock
    pico_stdlib
    pico_multicore
    pico_flash                              # flash_safe_execute
    # pico_cyw43_arch_lwip_threadsafe_background
    hardware_rtc
    hardware_pwm
    hardware_flash
    littlefs
)

pico_enable_stdio_usb(hslock 1)
pico_enable_stdio_uart(hslock 0)

pico_add_extra_outputs(hslock)