cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(field_mode C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(field_mode
    src/main.c
    src/dusb_link.c
    src/ti8x_file.c
    src/usbh_ti_vendor.c
    # add fatfs source files from your loader_mode fork here so both
    # firmwares agree on the flash layout, e.g.:
    # lib/fatfs/ff.c
    # lib/fatfs/ffunicode.c
    # src/flash_fatfs_driver.c   <- ported from oyama/pico-usb-flash-drive
)

target_include_directories(field_mode PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    # ${CMAKE_CURRENT_LIST_DIR}/lib/fatfs
)

# TinyUSB in HOST role for this build (device role belongs to loader_mode)
target_compile_definitions(field_mode PRIVATE
    CFG_TUSB_RHPORT0_MODE=OPT_MODE_HOST
    BOARD_TUH_RHPORT=0
)

target_link_libraries(field_mode
    pico_stdlib
    tinyusb_host
    tinyusb_board
)

pico_add_extra_outputs(field_mode)
