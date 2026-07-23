# Minimal external-project import for the Raspberry Pi Pico SDK.
# Set PICO_SDK_PATH in the environment or pass -DPICO_SDK_PATH=/path/to/pico-sdk.

if (DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()

set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK")

if (NOT PICO_SDK_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH to a Raspberry Pi Pico SDK checkout")
endif()

get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH)
if (NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "${PICO_SDK_PATH} does not contain pico_sdk_init.cmake")
endif()

include("${PICO_SDK_PATH}/pico_sdk_init.cmake")
