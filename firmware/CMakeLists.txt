cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()

if (NOT PICO_SDK_PATH)
    message(FATAL_ERROR
        "PICO_SDK_PATH is not set. Install the Pico SDK and point PICO_SDK_PATH at it. "
        "See firmware/README.md.")
endif()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(vendetta_hid C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

# One source tree, two chips. PICO_BOARD picks which, and the product string
# follows so the shell can tell the boards apart over serial.
if (PICO_PLATFORM MATCHES "rp2350")
    set(VENDETTA_CHIP "RP2350")
    set(VENDETTA_TARGET "vendetta-rp2350")
else()
    set(VENDETTA_CHIP "RP2040")
    set(VENDETTA_TARGET "vendetta-rp2040")
endif()

add_executable(${VENDETTA_TARGET}
    main.c
    usb_descriptors.c
)

target_include_directories(${VENDETTA_TARGET} PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_compile_definitions(${VENDETTA_TARGET} PRIVATE
    VENDETTA_PRODUCT="Vendetta ${VENDETTA_CHIP}"
)

target_link_libraries(${VENDETTA_TARGET} PRIVATE
    pico_stdlib
    pico_unique_id
    tinyusb_device
)

# The CDC interface in this firmware is the control channel, so the SDK must not
# also claim it for printf.
pico_enable_stdio_usb(${VENDETTA_TARGET} 0)
pico_enable_stdio_uart(${VENDETTA_TARGET} 0)

pico_add_extra_outputs(${VENDETTA_TARGET})
