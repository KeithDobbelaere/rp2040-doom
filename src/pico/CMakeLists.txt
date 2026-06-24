add_library(common_pico_base INTERFACE)

target_sources(common_pico_base INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_glob.c
        ${CMAKE_CURRENT_LIST_DIR}/i_input.c
        ${CMAKE_CURRENT_LIST_DIR}/i_system.c
        ${CMAKE_CURRENT_LIST_DIR}/piconet.c
        ${CMAKE_CURRENT_LIST_DIR}/i_timer.c
        ${CMAKE_CURRENT_LIST_DIR}/stubs.c
        ${CMAKE_CURRENT_LIST_DIR}/i_picosound.c
)

if (PICO_ON_DEVICE)
    target_sources(common_pico_base INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/blit.S
            ${CMAKE_CURRENT_LIST_DIR}/picoflash.c
    )

    pico_wrap_function(common_pico_base malloc)
    pico_wrap_function(common_pico_base calloc)
    pico_wrap_function(common_pico_base free)

    target_compile_definitions(common_pico_base INTERFACE
            PICO_HEAP_SIZE=0
            USE_ZONE_FOR_MALLOC=1
    )

    target_link_libraries(common_pico_base INTERFACE hardware_i2c)
endif()

target_include_directories(common_pico_base INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${CMAKE_CURRENT_LIST_DIR}/../../textscreen
)

target_compile_definitions(common_pico_base INTERFACE
        NO_USE_MOUSE=1
        PICO_AUDIO_I2S_PIO=1
        PICO_AUDIO_I2S_DMA_IRQ=1
        PICO_USE_SW_SPIN_LOCKS=0
        PICO_USE_STACK_GUARDS=0
)

target_link_libraries(common_pico_base INTERFACE
        pico_stdlib
        pico_multicore
)

# --------------------------------------------------------------------
# VGA / scanvideo backend
# --------------------------------------------------------------------

add_library(common_pico_vga INTERFACE)

target_sources(common_pico_vga INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_video.c
)

pico_generate_pio_header(common_pico_vga ${CMAKE_CURRENT_LIST_DIR}/video_doom.pio)

target_link_libraries(common_pico_vga INTERFACE
        common_pico_base
        pico_scanvideo_dpi
)

target_compile_definitions(common_pico_vga INTERFACE
        PICO_VIDEO_BACKEND_VGA=1
)

# Common Pico platform layer. Video backend is selected per executable.
add_library(common_pico INTERFACE)
target_link_libraries(common_pico INTERFACE common_pico_base)

# --------------------------------------------------------------------
# PicoCalc / ILI9488 SPI backend
# --------------------------------------------------------------------

add_library(common_pico_picocalc INTERFACE)

target_sources(common_pico_picocalc INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_video_picocalc.c
        ${CMAKE_CURRENT_LIST_DIR}/picocalc_display.c
)

target_link_libraries(common_pico_picocalc INTERFACE
        common_pico_base
        hardware_spi
        hardware_dma
        hardware_gpio
)

target_compile_definitions(common_pico_picocalc INTERFACE
        PICO_VIDEO_BACKEND_PICOCALC=1
)

add_library(pico_cd INTERFACE)
if (TARGET tinyusb_host)
    target_link_libraries(pico_cd INTERFACE tinyusb_host)
endif()