cmake_minimum_required(VERSION 3.28)

project(
    WinMTR
    VERSION 1.0.0.0
    DESCRIPTION "WinMTR v1.00 (Hyper Network Technology LTD)"
    LANGUAGES CXX RC
)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "WinMTR 只能使用 Microsoft C++ 工具鏈建置 Windows 版本。")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "WinMTR v1.00 僅支援 x64；請使用 -A x64 重新設定。")
endif()

if(CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR "WinMTR v1.00 僅支援 x64；目前平台為 ${CMAKE_GENERATOR_PLATFORM}。")
endif()

# The source currently uses C++23 library facilities such as std::expected and
# std::out_ptr in addition to named C++ module interface units.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# 1 selects the static MFC library. Release builds also use the static CRT so
# end users do not need to install an additional redistributable package.
set(CMAKE_MFC_FLAG 1)

file(GLOB WINMTR_CPP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
file(GLOB WINMTR_MODULE_INTERFACES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx"
)
file(GLOB WINMTR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
file(GLOB WINMTR_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
)

add_executable(WinMTR WIN32)

target_sources(WinMTR
    PRIVATE
        ${WINMTR_CPP_SOURCES}
        ${WINMTR_HEADERS}
        ${WINMTR_RESOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        ${WINMTR_MODULE_INTERFACES}
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/WinMTRConfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/WinMTRConfig.h"
    @ONLY
)

target_include_directories(WinMTR PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_compile_definitions(WinMTR PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    WINVER=0x0601
    _WIN32_WINNT=0x0601
)

target_compile_options(WinMTR PRIVATE
    /utf-8
    /permissive-
    /Zc:__cplusplus
    /translateInclude
)

set_property(TARGET WinMTR PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
set_property(TARGET WinMTR PROPERTY CXX_SCAN_FOR_MODULES ON)

target_link_options(WinMTR PRIVATE
    "/MANIFEST:EMBED"
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
)

# Use the desktop libraries available on Windows 7. Do not replace this list
# with onecore.lib: doing so adds Windows Runtime API-set imports unavailable on
# Windows 7.
target_link_libraries(WinMTR PRIVATE
    advapi32
    comctl32
    dnsapi
    gdi32
    gdiplus
    iphlpapi
    msimg32
    ole32
    oleaut32
    shell32
    shlwapi
    user32
    version
    winhttp
    winmm
    winspool
    ws2_32
)
