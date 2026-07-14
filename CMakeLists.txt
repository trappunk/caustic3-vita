cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Set VITASDK or CMAKE_TOOLCHAIN_FILE")
endif()
set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")

project(caustic3_vita C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Caustic 3 Vita")
set(VITA_TITLEID "CSTC00001")
set(VITA_VERSION "01.02")
set(VPK_DATA_ROOT "/tmp/caustic3-vpk-data" CACHE PATH "Space-free path to APK data")
set(CAUSTIC_PROFILE "demo3320" CACHE STRING "Caustic native binary profile")
option(CAUSTIC_INCLUDE_EXTRAS "Package locally supplied optional extras" OFF)

if(CAUSTIC_PROFILE STREQUAL "full322")
    add_compile_definitions(CAUSTIC_FULL_322=1)
elseif(NOT CAUSTIC_PROFILE STREQUAL "demo3320")
    message(FATAL_ERROR "Unknown CAUSTIC_PROFILE: ${CAUSTIC_PROFILE}")
endif()

if(CAUSTIC_INCLUDE_EXTRAS)
    foreach(EXTRA_DIRECTORY
            skins/newskin
            skins/Julia
            skins/Echo
            presets
            songs/demo)
        if(NOT EXISTS "${VPK_DATA_ROOT}/extras/${EXTRA_DIRECTORY}")
            message(FATAL_ERROR
                "CAUSTIC_INCLUDE_EXTRAS requires ${VPK_DATA_ROOT}/extras/${EXTRA_DIRECTORY}")
        endif()
    endforeach()
    add_compile_definitions(CAUSTIC_BUNDLED_EXTRAS=1)
    set(CAUSTIC_VPK_EXTRAS
        FILE ${VPK_DATA_ROOT}/extras/skins/newskin extra/skins/newskin
        FILE ${VPK_DATA_ROOT}/extras/skins/Julia extra/skins/Julia
        FILE ${VPK_DATA_ROOT}/extras/skins/Echo extra/skins/Echo
        FILE ${VPK_DATA_ROOT}/extras/presets extra/presets
        FILE ${VPK_DATA_ROOT}/extras/songs/demo extra/songs/demo)
else()
    set(CAUSTIC_VPK_EXTRAS)
endif()

add_compile_definitions(
    _GNU_SOURCE
    __POSIX_VISIBLE=999999
    DATA_PATH="ux0:/data/CAUSTIC3/"
)
add_compile_options(-O2 -g -mfloat-abi=softfp -mfpu=neon
    -Wno-incompatible-pointer-types -Wno-int-conversion)
add_link_options(-Wl,-q -Wl,--no-warn-mismatch -Wl,-z,max-page-size=0x2000)

add_executable(caustic3
    src/apk_extract.c
    src/main.c
    src/dynlib.c
    src/log.c
    src/falsojni_impl.c
    src/reimpl/ctype_patch.c
    src/reimpl/env.c
    src/reimpl/fortify.c
    src/reimpl/io.c
    src/reimpl/log.c
    src/reimpl/math_bridge.c
    src/reimpl/mem.c
    src/reimpl/path_policy.c
    src/reimpl/pthr.c
    src/reimpl/sys.c
    src/utils/glutil.c
    src/utils/logger.c
    src/utils/settings.c
    src/utils/utils.c
    lib/FalsoJNI/FalsoJNI.c
    lib/FalsoJNI/FalsoJNI_ImplBridge.c
    lib/FalsoJNI/FalsoJNI_Logger.c
    lib/FalsoJNI/converter.c
    lib/sha1/sha1.c
    lib/so_util/so_util.c
)

add_subdirectory(lib/libc_bridge)
add_dependencies(caustic3 SceLibcBridge)

target_include_directories(caustic3 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(caustic3
    -Wl,--whole-archive pthread -Wl,--no-whole-archive
    vitaGL
    vitashark
    SceShaccCgExt
    z
    m
    mathneon
    kubridge_stub
    taihen_stub
    SceShaccCgExt
    SceShaccCg_stub
    SceAppMgr_stub
    SceAppUtil_stub
    SceAudioIn_stub
    SceAudio_stub
    SceCtrl_stub
    SceCommonDialog_stub
    SceDisplay_stub
    SceGxm_stub
    SceIofilemgr_stub
    SceKernelDmacMgr_stub
    SceLibKernel_stub
    SceProcessmgr_stub
    SceSysmem_stub
    SceSysmodule_stub
    SceTouch_stub
    SceVshBridge_stub
    ${CMAKE_BINARY_DIR}/lib/libc_bridge/stubs/libSceLibcBridge_stub.a
    stdc++
)

vita_create_self(eboot.bin caustic3 UNSAFE NOASLR)
if(CAUSTIC_PROFILE STREQUAL "demo3320")
    set(CAUSTIC_VPK_ASSETS FILE ${VPK_DATA_ROOT}/assets assets)
else()
    set(CAUSTIC_VPK_ASSETS)
endif()
vita_create_vpk(Caustic3Vita.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    ${CAUSTIC_VPK_ASSETS}
    FILE ${VPK_DATA_ROOT}/sce_sys/icon0.png sce_sys/icon0.png
    FILE ${VPK_DATA_ROOT}/sce_sys/pic0.png sce_sys/pic0.png
    FILE ${VPK_DATA_ROOT}/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
    FILE ${VPK_DATA_ROOT}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE ${VPK_DATA_ROOT}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    ${CAUSTIC_VPK_EXTRAS}
    FILE ${VPK_DATA_ROOT}/libcaustic.so lib/libcaustic.so
    FILE ${VPK_DATA_ROOT}/caustic.apk caustic.apk
)
