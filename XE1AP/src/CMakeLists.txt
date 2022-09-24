cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(pcexe1ap C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

#set(PICO_EXAMPLES_PATH ${PROJECT_SOURCE_DIR})

# Initialize the SDK
pico_sdk_init()

set(FAMILY rp2040)

add_executable(pcexe1ap)

pico_add_extra_outputs(pcexe1ap)

pico_generate_pio_header(pcexe1ap ${CMAKE_CURRENT_LIST_DIR}/clock.pio )
pico_generate_pio_header(pcexe1ap ${CMAKE_CURRENT_LIST_DIR}/protocol.pio )
pico_generate_pio_header(pcexe1ap ${CMAKE_CURRENT_LIST_DIR}/multplex.pio )


# Example source
target_sources(pcexe1ap PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/hid_app.c
        ${CMAKE_CURRENT_SOURCE_DIR}/main.c
        )

# Example include
target_include_directories(pcexe1ap PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        )

target_link_libraries(pcexe1ap PRIVATE
	pico_stdlib
	pico_multicore
	hardware_pio
	tinyusb_host
	tinyusb_board
	)

