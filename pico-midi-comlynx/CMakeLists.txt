cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(alynxdj_midi_comlynx C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(COMLYNX_TX_PIN 2 CACHE STRING "Pico GPIO connected to the ComLynx output stage")
set(SERIAL_MIDI_RX_PIN 13 CACHE STRING "GPIO for opto-isolated 31.25 kbaud MIDI input; -1 disables it")
set(STATUS_LED_PIN -1 CACHE STRING "Active-high ready LED GPIO; -1 disables it")

add_executable(alynxdj_midi_comlynx
    main.c
    usb_descriptors.c
)

pico_generate_pio_header(alynxdj_midi_comlynx
    ${CMAKE_CURRENT_LIST_DIR}/comlynx_tx.pio
)

target_include_directories(alynxdj_midi_comlynx PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(alynxdj_midi_comlynx PRIVATE
    COMLYNX_TX_PIN=${COMLYNX_TX_PIN}
    SERIAL_MIDI_RX_PIN=${SERIAL_MIDI_RX_PIN}
    STATUS_LED_PIN=${STATUS_LED_PIN}
)

target_link_libraries(alynxdj_midi_comlynx PRIVATE
    pico_stdlib
    pico_unique_id
    hardware_pio
    hardware_uart
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_usb(alynxdj_midi_comlynx 0)
pico_enable_stdio_uart(alynxdj_midi_comlynx 0)
pico_add_extra_outputs(alynxdj_midi_comlynx)
