cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(field_mode C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# FatFs ships inside the Pico SDK's tinyusb submodule -- same source
# pico-usb-flash-drive uses, so both firmwares agree on the FAT12 format
# byte-for-byte. No separate FatFs download/clone needed.
set(FATFS_DIR ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source)

add_executable(field_mode
    src/main.c
    src/dusb_link.c
    src/ti8x_file.c
    src/usbh_ti_vendor.c
    src/flash_ro.c
    src/fatfs_driver_ro.c
    ${FATFS_DIR}/ff.c
    ${FATFS_DIR}/ffsystem.c
    ${FATFS_DIR}/ffunicode.c
)

target_include_directories(field_mode PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${FATFS_DIR}
)

# TinyUSB in HOST role for this build (device role belongs to loader_mode)
target_compile_definitions(field_mode PRIVATE
    CFG_TUSB_RHPORT0_MODE=OPT_MODE_HOST
    BOARD_TUH_RHPORT=0
)

target_link_libraries(field_mode
    pico_stdlib
    hardware_flash
    tinyusb_host
    tinyusb_board
)

# Debug output goes over UART (GP0=TX/GP1=RX at 115200 baud), never USB --
# the native USB port is dedicated to host mode talking to the calculator,
# so it can't also carry a USB-CDC debug console at the same time.
pico_enable_stdio_uart(field_mode 1)
pico_enable_stdio_usb(field_mode 0)

pico_add_extra_outputs(field_mode)
