cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(alynxdj_midi_comlynx C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(COMLYNX_TX_PIN 2 CACHE STRING "Pico GPIO connected to the ComLynx output stage")
set(SERIAL_MIDI_RX_PIN 13 CACHE STRING "GPIO for opto-isolated 31.25 kbaud MIDI input; -1 disables it")
set(STATUS_LED_PIN -1 CACHE STRING "Active-high ready LED GPIO; -1 disables it")
set(GG_SYNC_MODE OFF CACHE BOOL "Build the Chipbridge SMSGGDJ OUT to ALYNXDJ sync firmware")
set(GG_SYNC_INPUT_PIN 13 CACHE STRING "GPIO receiving the opto-isolated GG differential counter state")

if(GG_SYNC_MODE)
    set(FIRMWARE_TARGET alynxdj_gg_sync)
    add_executable(${FIRMWARE_TARGET}
        gg_sync_main.c
        gg_row_sync.c
    )
else()
    set(FIRMWARE_TARGET alynxdj_midi_comlynx)
    add_executable(${FIRMWARE_TARGET}
        main.c
        usb_descriptors.c
    )
endif()

pico_generate_pio_header(${FIRMWARE_TARGET}
    ${CMAKE_CURRENT_LIST_DIR}/comlynx_tx.pio
)

target_include_directories(${FIRMWARE_TARGET} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(${FIRMWARE_TARGET} PRIVATE
    COMLYNX_TX_PIN=${COMLYNX_TX_PIN}
    SERIAL_MIDI_RX_PIN=${SERIAL_MIDI_RX_PIN}
    STATUS_LED_PIN=${STATUS_LED_PIN}
    GG_SYNC_INPUT_PIN=${GG_SYNC_INPUT_PIN}
)

target_link_libraries(${FIRMWARE_TARGET} PRIVATE
    pico_stdlib
    pico_unique_id
    hardware_pio
)

if(NOT GG_SYNC_MODE)
    target_link_libraries(${FIRMWARE_TARGET} PRIVATE
        hardware_uart
        tinyusb_device
        tinyusb_board
    )
endif()

pico_enable_stdio_usb(${FIRMWARE_TARGET} 0)
pico_enable_stdio_uart(${FIRMWARE_TARGET} 0)
pico_add_extra_outputs(${FIRMWARE_TARGET})
