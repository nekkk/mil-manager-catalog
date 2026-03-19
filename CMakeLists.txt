cmake_minimum_required(VERSION 3.20)

if(DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO "$ENV{DEVKITPRO}" CACHE PATH "devkitPro root")
elseif(NOT DEFINED DEVKITPRO)
    set(DEVKITPRO "C:/devkitPro" CACHE PATH "devkitPro root")
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${DEVKITPRO}/cmake/Switch.cmake" CACHE STRING "")
endif()

project(mil_manager VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MIL_ENABLE_NXLINK "Enable nxlink stdout/stderr forwarding" OFF)

add_executable(${PROJECT_NAME}
    src/app.cpp
    src/catalog.cpp
    src/config.cpp
    src/graphics.cpp
    src/http.cpp
    src/installer.cpp
    src/main.cpp
    src/platform_switch.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    external/font8x8
    external/picojson
    ${DEVKITPRO}/portlibs/switch/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    CURL_STATICLIB
    $<$<BOOL:${MIL_ENABLE_NXLINK}>:MIL_ENABLE_NXLINK=1>
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    curl
    archive
    bz2
    lz4
    lzma
    mbedtls
    mbedcrypto
    mbedx509
    z
    zstd
)

nx_generate_nacp(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.nacp
    NAME "Gerenciador MIL Traducoes"
    AUTHOR "M.I.L."
    VERSION ${PROJECT_VERSION}
)

nx_create_nro(${PROJECT_NAME}
    NACP ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.nacp
    ROMFS ${CMAKE_CURRENT_SOURCE_DIR}/romfs
)
