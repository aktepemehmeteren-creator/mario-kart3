cmake_minimum_required(VERSION 3.11)

project(MarioKartPSP C)

add_executable(${PROJECT_NAME} main.c)

target_compile_options(${PROJECT_NAME} PRIVATE
    -O3
    -G0
    -Wall
    -fno-strict-aliasing
    -fsingle-precision-constant
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pspgum
    pspgu
    pspctrl
    pspdisplay
    pspge
    m
)

create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Mario Kart PSP - Cinematic Edition"
    VERSION "02.00"
    MEMSIZE 4
)
