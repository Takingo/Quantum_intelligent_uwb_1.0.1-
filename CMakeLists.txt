cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(UWB_TAG_FIRMWARE)

target_include_directories(app PRIVATE src/decadriver)
target_sources(app PRIVATE 
    src/main.c
    src/uwb_driver_qorvo.c
    src/decadriver/deca_device.c
    # src/decadriver/deca_params_init.c
    src/decadriver/platform_port.c
)
